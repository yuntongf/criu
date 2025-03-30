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
static int intrusive_mq_peek_all(int fd, struct mqueue_message *msgs, long nmsgs, long msgsize)
{
    unsigned int prio;
    int nmsg_read;
    int old_flags;
    
    char *buf = malloc(msgsize);
    if (!buf) {
        pr_err("Failed to allocate message buffer\n");
        return -1;
    }

    /* Set NONBLOCK temporarily to avoid hanging on empty queue */
    old_flags = fcntl(fd, F_GETFL);
    if (old_flags == -1) {
        pr_perror("fcntl F_GETFL failed");
        free(buf);
        return -1;
    }

    if (fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) == -1) {
        pr_perror("fcntl F_SETFL O_NONBLOCK failed");
        free(buf);
        return -1;
    }

    /* Intrusively peek messages from queue */
    for (nmsg_read = 0; nmsg_read < nmsgs; ++nmsg_read) {
        ssize_t len = mq_receive(fd, buf, msgsize, &prio);
        if (len < 0) {
            if (errno == EAGAIN)
                break;  // queue drained
            pr_perror("mq_receive failed");
            break;
        }

        msgs[nmsg_read].prio = prio;
        msgs[nmsg_read].len = len;
        msgs[nmsg_read].data = malloc(len);
        if (!msgs[nmsg_read].data) {
            pr_err("Failed to allocate msg data buffer\n");
            break;
        }
        memcpy(msgs[nmsg_read].data, buf, len);
    }

    /* Restore the messages */
    for (int j = 0; j < nmsg_read; j++) {
        if (mq_send(fd, (char *)msgs[j].data, msgs[j].len, msgs[j].prio) < 0) {
            pr_perror("mq_send failed while restoring messages during intrusive peek\n");
            free(buf);
            return -1;
        }
    }

    fcntl(fd, F_SETFL, old_flags);

    free(buf);
    return nmsg_read;
}


static int fill_mqueue_entry(MqueueEntry *entry, const char *name, const struct mq_attr *attr,
    struct mqueue_message *msgs, int nmsgs, const struct fd_parms *p, u32 id, int lfd)
{
    struct stat st;

    if (fstat(p->fd, &st) < 0) {
        pr_perror("fstat failed");
        return -1;
    }

    entry->id = id;
    entry->ino = st.st_ino;
    entry->mode = st.st_mode;
    entry->open_flags = p->flags;
    entry->fown = (FownEntry *)&p->fown;

    entry->name = (char *)name;
    entry->maxmsg = attr->mq_maxmsg;
    entry->msgsize = attr->mq_msgsize;
    entry->curmsgs = attr->mq_curmsgs;
    entry->mq_flags = attr->mq_flags;

    entry->msgs = calloc(nmsgs, sizeof(MqueueMessage *));
    if (!entry->msgs)
        return -1;

    for (int i = 0; i < nmsgs; i++) {
        MqueueMessage *msg = calloc(1, sizeof(MqueueMessage));
        if (!msg)
            return -1;
        mqueue_message__init(msg);
        msg->priority = msgs[i].prio;
        msg->body.data = (uint8_t *)msgs[i].data;
        msg->body.len = msgs[i].len;
        entry->msgs[i] = msg;
    }
    entry->n_msgs = nmsgs;

    return 0;
}

static int write_mqueue_image(const MqueueEntry *entry, u32 id)
{
    struct cr_img *img;
    FileEntry fe = FILE_ENTRY__INIT;
    fe.id = id;
    fe.type = FD_TYPES__PMQUEUE;
    fe.mqueue = (MqueueEntry *)entry;

    img = img_from_set(glob_imgset, CR_FD_FILES);
    if (!img) {
        pr_info("No image for mqueue\n");
        return -1;
    }

    return pb_write_one(img, &fe, PB_FILE);
}

int dump_mqueue_fd(struct fd_parms *p, int lfd, FdinfoEntry *e)
{
    struct mq_attr attr;
    struct mqueue_message *msgs = NULL;
    int nmsgs;
    u32 id;
    MqueueEntry mfe = MQUEUE_ENTRY__INIT;

    pr_info("Starting dump of mqueue FD %d (%s)\n", lfd, p->link ? p->link->name : "unknown");

    if (mq_getattr(lfd, &attr) < 0) {
        pr_perror("mq_getattr failed");
        return -1;
    }

    pr_info("mq_getattr: maxmsg=%ld, msgsize=%ld, curmsgs=%ld\n", 
        attr.mq_maxmsg, attr.mq_msgsize, attr.mq_curmsgs);

    msgs = calloc(attr.mq_curmsgs, sizeof(struct mqueue_message));
    if (!msgs)
        return -1;

    pr_info("Intrusively peek %ld messages\n", attr.mq_curmsgs);
    nmsgs = intrusive_mq_peek_all(lfd, msgs, attr.mq_curmsgs, attr.mq_msgsize);
    if (nmsgs < 0) {
        pr_perror("mq_peek_all failed");
        goto cleanup_msgs;
    }
    pr_info("Intrusively peeked %d messages from mqueue\n", nmsgs);

    fd_id_generate_special(p, &id);
    if (e) {
        e->type = FD_TYPES__PMQUEUE;
        e->fd = p->fd;
        e->id = id;
        e->flags = p->fd_flags;
    }

    if (fill_mqueue_entry(&mfe, p->link->name + 1, &attr, msgs, nmsgs, p, id, lfd) < 0) {
        pr_err("Failed to prepare mqueue entry\n");
        goto cleanup_msgs;
    }

    if (write_mqueue_image(&mfe, id) < 0) {
        pr_err("Failed to write mqueue image\n");
        goto cleanup_entry;
    }

    for (int i = 0; i < nmsgs; i++)
            free(mfe.msgs[i]);
    free(mfe.msgs);
    free(msgs);
    return 0;

cleanup_entry:
    for (int i = 0; i < nmsgs; i++)
    free(mfe.msgs[i]);
    free(mfe.msgs);
cleanup_msgs:
    for (int i = 0; i < attr.mq_curmsgs; i++)
        free(msgs[i].data);
    free(msgs);
    return -1;
}


static int mqueue_open(struct file_desc *d, int *new_fd)
{
	struct mqueue_file_info *info;
	MqueueEntry *mfe;
	struct mq_attr attr;
	mqd_t mqd;
    int oflags;
	int fd;

	info = container_of(d, struct mqueue_file_info, d);
	mfe = info->mfe;

	pr_info("Creating mqueue: %s\n", mfe->name);

	attr.mq_maxmsg  = mfe->maxmsg;
	attr.mq_msgsize = mfe->msgsize;
    attr.mq_flags   = 0;

    oflags = mfe->open_flags | mfe->mq_flags;

	mqd = mq_open(mfe->name, O_CREAT | oflags, mfe->mode, &attr);
	if (mqd == (mqd_t)-1) {
		pr_perror("Can't reopen mqueue %s", mfe->name);
		return -1;
	}

    pr_info("there are %ld messages in the queue\n", mfe->n_msgs);  

	for (int i = 0; i < mfe->n_msgs; i++) {
		MqueueMessage *msg = mfe->msgs[i];
		if (mq_send(mqd, (char *)msg->body.data, msg->body.len, msg->priority) < 0) {
			pr_perror("mq_send failed");
			mq_close(mqd);
			return -1;
		}
	}

	fd = (int)mqd;

	if (rst_file_params(fd, mfe->fown, mfe->open_flags)) {
		pr_perror("Can't restore params on mqueue %s", mfe->name);
		close(fd);
		return -1;
	}

	*new_fd = fd;
	return 0;
}

static struct file_desc_ops mqueue_desc_ops = {
	.type = FD_TYPES__PMQUEUE,
	.open = mqueue_open,
};

static int collect_one_mqueue(void *obj, ProtobufCMessage *msg, struct cr_img *i) {
    struct mqueue_file_info *info = obj;

    info->mfe = pb_msg(msg, MqueueEntry);

    return file_desc_add(&info->d, info->mfe->id, &mqueue_desc_ops);
}

struct collect_image_info mqueue_cinfo = {
	.fd_type = CR_FD_MQUEUE,
	.pb_type = PB_MQUEUE,
	.priv_size = sizeof(struct mqueue_file_info),
	.collect = collect_one_mqueue,
};