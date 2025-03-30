#ifndef __CR_MQUEUE_H__
#define __CR_MQUEUE_H__

#include <mqueue.h>
#include "files.h"

struct mqueue_message {
    unsigned int prio;
    size_t len;
    char *data;
};

extern struct collect_image_info mqueue_cinfo;

struct mqueue_file_info {
	MqueueEntry *mfe;
	struct file_desc d;
	int fd;
	struct list_head rlist;
};

extern int dump_mqueue_fd(struct fd_parms *p, int lfd, FdinfoEntry *e);

#endif /* __CR_MQUEUE_H__ */
