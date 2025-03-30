#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "zdtmtst.h"

const char *test_doc = "Check that POSIX shared memory works correctly";
const char *test_author = "August Fu <yuntongf@gmail.com>";

int main(int argc, char **argv) {
    int fd;

    test_init(argc, argv);

    fd = shm_open("/myshm", O_CREAT | O_RDWR, 0644);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }

    test_daemon();
    test_waitsig();

    close(fd);
    shm_unlink("/myshm");

    pass();

    return 0;
}