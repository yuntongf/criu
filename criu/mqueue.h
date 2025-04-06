#ifndef __CR_MQUEUE_H__
#define __CR_MQUEUE_H__

#include <mqueue.h>
#include "files.h"

struct mqueue_message {
    unsigned int prio;
    size_t len;
    char *data;
};

extern struct collect_image_info pmqfd_cinfo;

struct mqueue_file_info {
	PmqfdEntry *mfe;
	struct file_desc d;
	int fd;
	struct list_head rlist;
};

static inline uint32_t hash_mqueue_name(const char *name) {
    uint32_t hash = 5381;
    int c;

    while ((c = *name++))
        hash = ((hash << 5) + hash) + c;

    return hash;
}

extern int dump_pmq_fd(int lfd, struct fd_parms *p, FdinfoEntry *e);

extern int intrusive_mq_peek_all(int fd, struct mqueue_message *msgs, long nmsgs, long msgsize);

#endif /* __CR_MQUEUE_H__ */
