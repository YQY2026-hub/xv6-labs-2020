#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    // 参数校验：必须传入1个数字参数
    if (argc != 2) {
        fprintf(2, "Usage: sleep ticks\n");
        exit(1);
    }

    // 字符串转整数，调用内核 sleep 系统调用
    int ticks = atoi(argv[1]);
    sleep(ticks);

    exit(0);
}
