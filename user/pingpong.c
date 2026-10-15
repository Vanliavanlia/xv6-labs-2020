#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define RD 0
#define WR 1

int main(int argc, char *argv[]) {
    int p2c[2];  // parent -> child
    int c2p[2];  // child  -> parent
    char buf = 'P';

    pipe(p2c);
    pipe(c2p);

    int pid = fork();

    if (pid < 0) {
        fprintf(2, "fork error\n");
        exit(1);
    } else if (pid == 0) {
        // 子进程
        close(p2c[WR]);   // 子进程不写 p2c
        close(c2p[RD]);   // 子进程不读 c2p

        if (read(p2c[RD], &buf, 1) != 1) {
            fprintf(2, "child read error\n");
            exit(1);
        }
        printf("%d: received ping\n", getpid());

        if (write(c2p[WR], &buf, 1) != 1) {
            fprintf(2, "child write error\n");
            exit(1);
        }

        close(p2c[RD]);
        close(c2p[WR]);
        exit(0);
    } else {
        // 父进程
        close(p2c[RD]);   // 父进程不读 p2c
        close(c2p[WR]);   // 父进程不写 c2p

        if (write(p2c[WR], &buf, 1) != 1) {
            fprintf(2, "parent write error\n");
            exit(1);
        }

        if (read(c2p[RD], &buf, 1) != 1) {
            fprintf(2, "parent read error\n");
            exit(1);
        }
        printf("%d: received pong\n", getpid());

        close(p2c[WR]);
        close(c2p[RD]);
        wait(0);
        exit(0);
    }
}