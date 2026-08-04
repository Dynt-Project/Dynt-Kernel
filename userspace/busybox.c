/*
 * Dynt-Kernel BusyBox — a minimal multi-call binary for userspace.
 *
 * This program is loaded by the kernel as /init from the FAT32 partition.
 * It implements a simple interactive shell with built-in applets:
 *   echo, cat, ls, help, clear, uname, whoami, hostname, true, false,
 *   yes, sleep, reboot, exit
 *
 * Build: gcc -static -nostdlib -no-pie -T userspace/linker.ld -o init busybox.c
 */

#include "stdio.h"
#include "string.h"
#include "syscall.h"

#define MAX_CMD  256
#define MAX_ARGS 16
#define MAX_LINE 512

/* ---- applets ---- */

static int applet_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        write(argv[i], strlen(argv[i]));
        if (i + 1 < argc)
            write(" ", 1);
    }
    write("\n", 1);
    return 0;
}

static int applet_cat(int argc, char **argv)
{
    if (argc < 2)
    {
        write("cat: missing file\n", 19);
        return 1;
    }

    static char buf[8192];

    for (int i = 1; i < argc; i++)
    {
        long ret = syscall3(SYS_READ_FILE, (long)argv[i], (long)buf, sizeof(buf));
        if (ret < 0)
        {
            write("cat: ", 5);
            write(argv[i], strlen(argv[i]));
            write(": no such file\n", 15);
            return 1;
        }
        write(buf, (unsigned long)ret);
    }

    return 0;
}

static int applet_ls(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/";
    static char buf[4096];

    long ret = syscall3(SYS_LIST_DIR, (long)path, (long)buf, sizeof(buf));
    if (ret < 0)
    {
        write("ls: cannot list directory\n", 26);
        return 1;
    }

    write(buf, (unsigned long)ret);
    return 0;
}

static int applet_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("Dynt-Kernel BusyBox v1.0");
    puts("Available commands:");
    puts("  echo [text]    - print text");
    puts("  cat <file>     - print file contents");
    puts("  ls [path]      - list directory");
    puts("  help           - this message");
    puts("  clear          - clear screen");
    puts("  uname          - print system info");
    puts("  whoami         - print current user");
    puts("  hostname       - print hostname");
    puts("  true           - exit 0");
    puts("  false          - exit 1");
    puts("  yes [text]     - repeat text forever");
    puts("  exec <file>    - run a userspace program");
    puts("  exit           - exit shell");
    return 0;
}

static int applet_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    write("\x1b[2J\x1b[H", 7);
    return 0;
}

static int applet_uname(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("Dynt-Kernel 1.0 x86_64 GNU/Linux");
    return 0;
}

static int applet_whoami(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("root");
    return 0;
}

static int applet_hostname(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("dynt");
    return 0;
}

static int applet_true(int argc, char **argv)
{
    (void)argc; (void)argv;
    return 0;
}

static int applet_false(int argc, char **argv)
{
    (void)argc; (void)argv;
    return 1;
}

static int applet_yes(int argc, char **argv)
{
    const char *msg = argc > 1 ? argv[1] : "y";
    for (;;)
    {
        write(msg, strlen(msg));
        write("\n", 1);
    }
    return 0;
}

static int applet_exit(int argc, char **argv)
{
    (void)argc; (void)argv;
    syscall0(SYS_EXIT);
    return 0;
}

static int applet_exec(int argc, char **argv)
{
    if (argc < 2)
    {
        write("exec: missing file\n", 19);
        return 1;
    }

    long ret = syscall1(SYS_EXEC, (long)argv[1]);
    if (ret < 0)
    {
        write("exec: ", 6);
        write(argv[1], strlen(argv[1]));
        write(": cannot execute\n", 17);
        return 1;
    }

    return 0;
}

/* ---- command table ---- */

typedef int (*applet_fn)(int argc, char **argv);

struct applet
{
    const char *name;
    applet_fn   fn;
};

static const struct applet applets[] =
{
    { "echo",     applet_echo },
    { "cat",      applet_cat },
    { "ls",       applet_ls },
    { "help",     applet_help },
    { "clear",    applet_clear },
    { "uname",    applet_uname },
    { "whoami",   applet_whoami },
    { "hostname", applet_hostname },
    { "true",     applet_true },
    { "false",    applet_false },
    { "yes",      applet_yes },
    { "exit",     applet_exit },
    { "exec",     applet_exec },
    { 0, 0 }
};

static applet_fn find_applet(const char *name)
{
    for (const struct applet *a = applets; a->name; a++)
    {
        if (strcmp(a->name, name) == 0)
            return a->fn;
    }
    return 0;
}

/* ---- shell ---- */

static void parse_args(char *line, int *argc, char **argv, int max_args)
{
    *argc = 0;
    char *p = line;

    while (*p && *argc < max_args - 1)
    {
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == 0)
            break;

        argv[(*argc)++] = p;

        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;

        if (*p)
            *p++ = 0;
    }

    argv[*argc] = 0;
}

static void shell_loop(void)
{
    static char line[MAX_LINE];
    static char cmd[MAX_CMD];

    for (;;)
    {
        write("dynt# ", 6);

        /* Read a line from keyboard */
        long n = read(line, sizeof(line) - 1);
        if (n <= 0)
        {
            write("\n", 1);
            continue;
        }

        line[n] = 0;

        /* Strip trailing newline */
        if (n > 0 && line[n - 1] == '\n')
            line[n - 1] = 0;

        if (line[0] == 0)
            continue;

        /* Copy to cmd buffer for tokenization */
        strcpy(cmd, line);

        int argc;
        char *argv[MAX_ARGS];
        parse_args(cmd, &argc, argv, MAX_ARGS);

        if (argc == 0)
            continue;

        /* Try to find and run an applet */
        applet_fn fn = find_applet(argv[0]);
        if (fn)
        {
            fn(argc, argv);
        }
        else
        {
            write(argv[0], strlen(argv[0]));
            write(": command not found\n", 20);
        }
    }
}

/* ---- entry point ---- */

void _start(void)
{
    write("\n", 1);
    write("========================================\n", 41);
    write("  Dynt-Kernel BusyBox v1.0\n", 27);
    write("  Userspace Ring 3 Environment\n", 31);
    write("========================================\n", 41);
    write("\n", 1);

    printf("PID: %d\n", (int)syscall0(SYS_GETPID));
    write("Type 'help' for available commands.\n\n", 35);

    shell_loop();

    syscall0(SYS_EXIT);
}