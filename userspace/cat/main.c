#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: cat <file>\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "r");

    if (!f)
    {
        printf("cat: %s: not found\n", argv[1]);
        return 1;
    }

    char buf[512];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        fwrite(buf, 1, n, stdout);

    fclose(f);
    return 0;
}
