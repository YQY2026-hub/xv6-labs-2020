#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int p2c[2];  // 父→子管道：父写[1]，子读[0]
    int c2p[2];  // 子→父管道：子写[1]，父读[0]
    char buf;

    // 创建两个管道
    pipe(p2c);
    pipe(c2p);

    if (fork() == 0) {
        // ========== 子进程 ==========
        // 关闭子进程用不到的管道端
        close(p2c[1]);  // 子不写父→子管道
        close(c2p[0]);  // 子不读子→父管道

        // 读取父进程发来的1个字节
        read(p2c[0], &buf, 1);
        printf("%d: received ping\n", getpid());

        // 给父进程回发1个字节
        write(c2p[1], &buf, 1);

        // 关闭剩余管道端后退出
        close(p2c[0]);
        close(c2p[1]);
        exit(0);
    } else {
        // ========== 父进程 ==========
        // 关闭父进程用不到的管道端
        close(p2c[0]);  // 父不读父→子管道
        close(c2p[1]);  // 父不写子→父管道

        // 先给子进程发送1个字节
        write(p2c[1], "a", 1);

        // 等待子进程回传的字节
        read(c2p[0], &buf, 1);
        printf("%d: received pong\n", getpid());

        // 等待子进程完全退出，避免僵尸进程
        wait(0);

        close(p2c[1]);
        close(c2p[0]);
        exit(0);
    }
}
