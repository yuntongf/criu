#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>

#include "zdtmtst.h"

const char *test_doc = "Check that POSIX message queue works correctly";
const char *test_author = "August Fu <yuntongf@gmail.com>";
char *mqname = "/testmq";

int main(int argc, char **argv)
{
    mqd_t mqd;
    struct mq_attr attr;
    struct mq_attr attr_check;
    char buf[128];
    unsigned int prio;
    char msg[] = "test message";

    test_init(argc, argv);

    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = 128;
    attr.mq_curmsgs = 0;

    printf("Opening message queue: %s\n", mqname);
    mqd = mq_open(mqname, O_CREAT | O_RDWR | O_NONBLOCK, 0644, &attr);
    if (mqd == (mqd_t)-1) {
        pr_perror("Can't create message queue \"%s\"", mqname);
        exit(1);
    }

    printf("Sending message to queue: %s\n", msg);
    if (mq_send(mqd, msg, strlen(msg) + 1, 0) == -1) {
        pr_perror("Can't send message to queue \"%s\"", mqname);
        mq_close(mqd);
        mq_unlink(mqname);
        exit(1);
    }

    printf("mqueue fd is %d\n", mqd);

    if (mq_getattr(mqd, &attr) == -1) {
        pr_perror("Failed to get attributes of original queue");
        mq_close(mqd);
        mq_unlink(mqname);
        exit(1);
    }

    test_daemon();
    test_waitsig();

    printf("Checking message queue attributes\n");
    if (mq_getattr(mqd, &attr_check) == -1) {
        pr_perror("Can't get attributes of message queue \"%s\"", mqname);
        mq_close(mqd);
        mq_unlink(mqname);
        exit(1);
    }

    if (attr_check.mq_flags != attr.mq_flags) {
        fail("Mismatch: mq_flags (expected: %ld, actual: %ld)",
             attr.mq_flags, attr_check.mq_flags);
        exit(1);
    } else if (attr_check.mq_maxmsg != attr.mq_maxmsg) {
        fail("Mismatch: mq_maxmsg (expected: %ld, actual: %ld)",
             attr.mq_maxmsg, attr_check.mq_maxmsg);
    } else if (attr_check.mq_msgsize != attr.mq_msgsize) {
        fail("Mismatch: mq_msgsize (expected: %ld, actual: %ld)",
             attr.mq_msgsize, attr_check.mq_msgsize);
    } else if (attr_check.mq_curmsgs != attr.mq_curmsgs) {
        fail("Mismatch: mq_curmsgs (expected: %ld, actual: %ld)",
             attr.mq_curmsgs, attr_check.mq_curmsgs);
    } 

    printf("Receiving message from queue\n");
    if (mq_receive(mqd, buf, sizeof(buf), &prio) == -1) {
        fail("Did not receive message from queue \"%s\"", mqname);
        mq_close(mqd);
        mq_unlink(mqname);
        exit(1);
    }

    printf("Comparing received message: %s\n", buf);
    if (strcmp(buf, msg)) {
        fail("Received message \"%s\" != sent message \"%s\"", buf, msg);
        exit(1);
    } else {
        pass();
    }

    mq_close(mqd);
    mq_unlink(mqname);

    return 0;
}