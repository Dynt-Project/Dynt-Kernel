#include <stdio.h>

#include "../dynt_syscall.h"

int main(void)
{
    char buf[4096];
    long n = dynt_syscall2(DYNT_SYS_PS, (long)buf, sizeof(buf));

    if (n < 0)
    {
        printf("ps failed\n");
        return 1;
    }

    for (long i = 0; i < n; i++)
        putchar(buf[i]);

    return 0;
}
