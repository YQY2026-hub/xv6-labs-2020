#include "kernel/types.h"
#include "user/user.h"

// 递归素数筛选函数，参数是左侧管道的读端
void sieve(int left_read_fd)
{
    int prime;
    // 从左侧管道读取第一个数，这个数一定是素数
    if (read(left_read_fd, &prime, sizeof(int)) != sizeof(int)) {
        close(left_read_fd);
        exit(0);
    }
    printf("prime %d\n", prime);

    int right_pipe[2];
    pipe(right_pipe); // 创建右侧管道，传给下一级子进程

    if (fork() == 0) {
        // ========== 子进程：进入下一级筛选 ==========
        close(left_read_fd);      // 关闭本级不用的左侧读端
        close(right_pipe[1]);     // 关闭本级不用的右侧写端
        sieve(right_pipe[0]);     // 递归处理右侧管道
    } else {
        // ========== 父进程：过滤数字并写入右侧 ==========
        close(right_pipe[0]);     // 关闭本级不用的右侧读端
        int num;
        // 循环读取左侧剩余数字，过滤掉当前素数的倍数
        while (read(left_read_fd, &num, sizeof(int)) == sizeof(int)) {
            if (num % prime != 0) {
                write(right_pipe[1], &num, sizeof(int));
            }
        }
        // 读完后关闭所有管道，等待子进程退出
        close(left_read_fd);
        close(right_pipe[1]);
        wait(0);
        exit(0);
    }
}

int main(int argc, char *argv[])
{
    int init_pipe[2];
    pipe(init_pipe);

    if (fork() == 0) {
        // 子进程：启动第一级素数筛选
        close(init_pipe[1]);  // 关闭不用的写端
        sieve(init_pipe[0]);
    } else {
        // 父进程：向初始管道写入 2 到 35 的所有整数
        close(init_pipe[0]);  // 关闭不用的读端
        for (int i = 2; i <= 35; i++) {
            write(init_pipe[1], &i, sizeof(int));
        }
        close(init_pipe[1]);  // 写完必须关闭写端，否则子进程会一直阻塞
        wait(0);              // 等待所有子进程全部退出
        exit(0);
    }
}
