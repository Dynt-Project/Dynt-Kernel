#include <stdio.h>
#include <string.h>

#include "../dynt_syscall.h"

int main(int argc, char **argv)
{
    const char *path = "/";

    if (argc > 1)
        path = argv[1];

    char buf[8192];
    long n = dynt_syscall3(DYNT_SYS_LIST_DIR, (long)path, (long)buf,
                           (long)sizeof(buf));

    if (n < 0)
    {
        printf("ls: cannot list %s\n", path);
        return 1;
    }

    for (long i = 0; i < n; i++)
        putchar(buf[i]);

    return 0;
}
