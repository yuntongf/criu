#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>

#include "zdtmtst.h"

const char *test_doc = "Check that POSIX message queue works correctly with one message";
const char *test_author = "August Fu <yuntongf@gmail.com>";
char *mqname = "/testmq";

int main(int argc, char **argv)
{
    mqd_t mqd;
    struct mq_attr attr;
    struct mq_attr attr_check;
    char buf[128];
    unsigned int prio;
    const char *msg1 = "first message";
    const char *msg2 = "second message";

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

    printf("Sending message to queue: %s\n", msg1);
    if (mq_send(mqd, msg1, strlen(msg1) + 1, 0) == -1) {
        pr_perror("Can't send message to queue \"%s\"", mqname);
        goto err;
    }

    printf("Sending message to queue: %s\n", msg2);
    if (mq_send(mqd, msg2, strlen(msg2) + 1, 0) == -1) {
        pr_perror("Can't send second message to queue \"%s\"", mqname);
        goto err;
    }

    printf("mqueue fd is %d\n", mqd);

    if (mq_getattr(mqd, &attr) == -1) {
        pr_perror("Failed to get attributes of original queue");
        goto err;
    }

    test_daemon();
    test_waitsig();

    printf("Checking message queue attributes\n");
    if (mq_getattr(mqd, &attr_check) == -1) {
        pr_perror("Can't get attributes of message queue \"%s\"", mqname);
        goto err;
    }

    if (attr_check.mq_flags != attr.mq_flags) {
        fail("Mismatch: mq_flags (expected: %ld, actual: %ld)",
             attr.mq_flags, attr_check.mq_flags);
        goto err;
    } else if (attr_check.mq_maxmsg != attr.mq_maxmsg) {
        fail("Mismatch: mq_maxmsg (expected: %ld, actual: %ld)",
             attr.mq_maxmsg, attr_check.mq_maxmsg);
        goto err;
    } else if (attr_check.mq_msgsize != attr.mq_msgsize) {
        fail("Mismatch: mq_msgsize (expected: %ld, actual: %ld)",
             attr.mq_msgsize, attr_check.mq_msgsize);
        goto err;
    } else if (attr_check.mq_curmsgs != attr.mq_curmsgs) {
        fail("Mismatch: mq_curmsgs (expected: %ld, actual: %ld)",
             attr.mq_curmsgs, attr_check.mq_curmsgs);
        goto err;
    } 

    printf("Receiving first message from queue\n");
    if (mq_receive(mqd, buf, sizeof(buf), &prio) == -1) {
        fail("Did not receive first message from queue \"%s\"", mqname);
        goto err;
    }
    printf("First received message: %s\n", buf);
    if (strcmp(buf, msg1)) {
        fail("First received message \"%s\" != expected \"%s\"", buf, msg1);
        goto err;
    }

    printf("Receiving second message from queue\n");
    if (mq_receive(mqd, buf, sizeof(buf), &prio) == -1) {
        fail("Did not receive second message from queue \"%s\"", mqname);
        goto err;
    }
    printf("Second received message: %s\n", buf);
    if (strcmp(buf, msg2)) {
        fail("Second received message \"%s\" != expected \"%s\"", buf, msg2);
        goto err;
    }

    printf("All messages received successfully\n");
    pass();

    mq_close(mqd);
    mq_unlink(mqname);

    return 0;

err:
    mq_close(mqd);
    mq_unlink(mqname);
    exit(1);
}