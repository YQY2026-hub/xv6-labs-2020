#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    char buf[512];
    char *exec_argv[MAXARG];
    int base_argc;
    int buf_pos = 0;

    // 参数校验：至少要指定一个要执行的命令
    if (argc < 2) {
        fprintf(2, "Usage: xargs command [args...]\n");
        exit(1);
    }

    // 把命令行传入的命令和参数，先复制到执行参数数组里
    for (base_argc = 0; base_argc < argc - 1; base_argc++) {
        exec_argv[base_argc] = argv[base_argc + 1];
    }

    // 逐字符读取标准输入
    while (read(0, &buf[buf_pos], 1) == 1) {
        if (buf[buf_pos] == '\n') {
            // 读到换行符，代表一行结束
            buf[buf_pos] = '\0'; // 补字符串结束符
            // 把当前行作为一个参数，追加到参数数组末尾
            exec_argv[base_argc] = buf;
            exec_argv[base_argc + 1] = 0; // argv必须以空指针结尾

            if (fork() == 0) {
                // 子进程：执行目标命令
                exec(exec_argv[0], exec_argv);
                fprintf(2, "xargs: exec %s failed\n", exec_argv[0]);
                exit(1);
            } else {
                // 父进程：等待子进程执行完毕
                wait(0);
            }

            // 重置缓冲区位置，准备读取下一行
            buf_pos = 0;
        } else {
            buf_pos++;
        }
    }

    exit(0);
}
