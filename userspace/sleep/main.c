#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: sleep <seconds>\n");
        return 1;
    }

    unsigned s = (unsigned)atoi(argv[1]);
    sleep(s);
    printf("slept %u second%s\n", s, s == 1 ? "" : "s");
    return 0;
}
