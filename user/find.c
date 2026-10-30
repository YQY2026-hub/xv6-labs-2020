#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 递归查找函数：在 path 路径下查找名为 target 的文件
void find(char *path, char *target)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    // 打开当前路径
    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    // 获取文件/目录的状态信息
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    // 情况1：当前路径是普通文件
    if (st.type == T_FILE) {
        // 提取路径中最后的文件名
        char *filename = path;
        char *slash = strchr(path, '/');
        while (slash != 0) {
            filename = slash + 1;
            slash = strchr(filename, '/');
        }
        // 字符串比较必须用 strcmp，不能用 ==
        if (strcmp(filename, target) == 0) {
            printf("%s\n", path);
        }
        close(fd);
        return;
    }

    // 情况2：当前路径是目录，遍历所有子项递归查找
    if (st.type == T_DIR) {
        // 检查路径长度，防止缓冲区溢出
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
            printf("find: path too long\n");
            close(fd);
            return;
        }
        // 拼接子路径：原路径 + / + 子项名称
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';

        // 循环读取目录项
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0)
                continue;
            // 跳过 . 和 .. ，避免无限递归
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            // 拼接完整子路径
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            // 递归进入子目录/子文件
            find(buf, target);
        }
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    // 参数校验：程序名 + 起始路径 + 目标文件名，共3个参数
    if (argc != 3) {
        fprintf(2, "Usage: find <path> <filename>\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}
