#include <mqueue.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include "protobuf.h"
#include "protobuf-desc.h"
#include "cr_options.h"
#include "namespaces.h"
#include "imgset.h"
#include "files.h"
#include "proc_parse.h"
#include "util.h"
#include "log.h"
#include "file-ids.h"
#include "mqueue.h"
#include "files-reg.h"
#include "mount.h"

/* This function is temporary and will be changed to a kernel interface similar to MSG_PEEK for sockets */
int intrusive_mq_peek_all(int fd, struct mqueue_message *msgs, long nmsgs, long msgsize)
{
    unsigned int prio;
    int nmsg_read = 0;
    int old_flags = -1;
    char *buf = NULL;

    buf = malloc(msgsize);
    if (!buf) {
        pr_err("Failed to allocate message buffer of size %ld\n", msgsize);
        return -1;
    }

    /* Set NONBLOCK temporarily to avoid hanging on empty queue */
    old_flags = fcntl(fd, F_GETFL);
    if (old_flags == -1) {
        pr_perror("fcntl F_GETFL failed");
        goto cleanup;
    }

    if (fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) == -1) {
        pr_perror("fcntl F_SETFL O_NONBLOCK failed");
        goto cleanup;
    }

    /* Intrusively peek messages from queue */
    while (nmsg_read < nmsgs) {
        ssize_t len = mq_receive(fd, buf, msgsize, &prio);
        if (len < 0) {
            if (errno == EAGAIN) {
                pr_info("Queue drained after reading %d messages\n", nmsg_read);
                break;
            }
            pr_perror("mq_receive failed during intrusive peek");
            goto restore;
        }

        msgs[nmsg_read].prio = prio;
        msgs[nmsg_read].len = len;
        msgs[nmsg_read].data = malloc(len);
        if (!msgs[nmsg_read].data) {
            pr_err("Failed to allocate memory for message %d\n", nmsg_read);
            goto restore;
        }

        memcpy(msgs[nmsg_read].data, buf, len);
        nmsg_read++;
    }

restore:
    for (int i = 0; i < nmsg_read; i++) {
        if (mq_send(fd, (char *)msgs[i].data, msgs[i].len, msgs[i].prio) < 0) {
            pr_perror("mq_send failed while restoring message %d", i);
            goto cleanup;
        }
    }

    if (old_flags != -1)
        fcntl(fd, F_SETFL, old_flags);
    free(buf);
    return nmsg_read;

cleanup:
    if (old_flags != -1)
        fcntl(fd, F_SETFL, old_flags);
    free(buf);
    return -1;
}


int dump_pmq_fd(int lfd, struct fd_parms *p, FdinfoEntry *e) {
    PmqfdEntry mfe = PMQFD_ENTRY__INIT;
    FileEntry fe = FILE_ENTRY__INIT;
    struct mq_attr attr;
    struct cr_img *img;

    if (lfd < 0) {
        pr_err("Invalid lfd to dump_mqueue_fd\n");
        return -1;
    }

    pr_info("Dumping POSIX message queue FD %d (%s)\n", lfd, p->link->name + 1);

    if (fd_id_generate_special(p, &mfe.id) < 0) {
        pr_err("Failed to generate special ID for PmqfdEntry\n");
        return -1;
    }
    mfe.open_flags = p->flags;
    mq_getattr(lfd, &attr);
    mfe.mq_flags = attr.mq_flags;
    mfe.name = p->link->name + 1;
    mfe.fown = (FownEntry *)&p->fown;

    fe.id = mfe.id;
    fe.type = FD_TYPES__PMQFD;
    fe.pmqfd = &mfe;

    if (e) {
        e->type = FD_TYPES__PMQFD;
        e->id = mfe.id;
        e->fd = p->fd;
        e->flags = p->fd_flags;
    }

    img = img_from_set(glob_imgset, CR_FD_FILES);
    if (!img) {
        pr_info("No image for mqueue\n");
        return -1;
    }

    return pb_write_one(img, &fe, PB_FILE);
}

static int open_pmq_fd(struct file_desc *d, int *new_fd)
{
	struct mqueue_file_info *info;
	PmqfdEntry *mfe;
	mqd_t mqd;
	int fd;

	info = container_of(d, struct mqueue_file_info, d);
	mfe = info->mfe;

	pr_info("Opening mqueue: %s with flag %d\n", mfe->name, mfe->open_flags);

	mqd = mq_open(mfe->name, mfe->open_flags | mfe->mq_flags);
	if (mqd == (mqd_t)-1) {
		pr_perror("Can't open mqueue %s", mfe->name);
		return -1;
	}

	fd = (int)mqd;

	if (rst_file_params(fd, mfe->fown, mfe->open_flags | mfe->mq_flags)) {
		pr_perror("Can't restore params on mqueue %s", mfe->name);
		close(fd);
		return -1;
	}

	*new_fd = fd;
	return 0;
}

static struct file_desc_ops pmqfd_desc_ops = {
	.type = FD_TYPES__PMQFD,
	.open = open_pmq_fd,
};

static int collect_one_pmq_fd(void *obj, ProtobufCMessage *msg, struct cr_img *i) {
    struct mqueue_file_info *info = obj;

    info->mfe = pb_msg(msg, PmqfdEntry);

    return file_desc_add(&info->d, info->mfe->id, &pmqfd_desc_ops);
}

struct collect_image_info pmqfd_cinfo = {
	.fd_type = CR_FD_PMQFD,
	.pb_type = PB_PMQFD,
	.priv_size = sizeof(struct mqueue_file_info),
	.collect = collect_one_pmq_fd,
};