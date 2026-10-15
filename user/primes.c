#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define RD 0
#define WR 1
#define INT_LEN sizeof(int)

// 每个 primes 进程的处理逻辑
void primes(int lpipe[2]) {
    close(lpipe[WR]);   // 只需要从左管道读

    int first;
    if (read(lpipe[RD], &first, INT_LEN) != INT_LEN) {
        close(lpipe[RD]);
        exit(0);
    }
    printf("prime %d\n", first);

    int p[2];           // 通往右边的新管道
    pipe(p);

    int data;
    while (read(lpipe[RD], &data, INT_LEN) == INT_LEN) {
        if (data % first != 0) {
            write(p[WR], &data, INT_LEN);
        }
    }

    close(lpipe[RD]);
    close(p[WR]);

    if (fork() == 0) {
        primes(p);      // 递归：在新进程中继续筛
    } else {
        close(p[RD]);
        wait(0);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    int p[2];
    pipe(p);

    for (int i = 2; i <= 35; i++) {
        write(p[WR], &i, INT_LEN);
    }

    if (fork() == 0) {
        primes(p);
    } else {
        close(p[WR]);
        close(p[RD]);
        wait(0);
    }
    exit(0);
}