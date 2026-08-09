//
// Dynt bash: a minimal bash-like shell.
//
// Reads a line, splits it into arguments and runs the program of that
// name from the root filesystem (e.g. "echo" -> "/echo"). Builtins:
// help, clear, exit. A foreground program can be stopped with Ctrl+C.
//
// init starts one bash instance per virtual terminal; the terminal
// number is passed as argv[1].
//

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "../dynt_syscall.h"

static void print_help(void)
{
    printf("Dynt bash - builtin commands:\n");
    printf("  help          show this help\n");
    printf("  clear         clear the screen\n");
    printf("  exit          quit the shell\n");
    printf("everything else: type the name of a program in / to run it,\n");
    printf("e.g.  echo hello   cat /foo   ls   sleep 2\n");
}

static void run_program(char **argv)
{
    pid_t pid = fork();

    if (pid == 0)
    {
        char path[256];
        snprintf(path, sizeof(path), "/%s", argv[0]);

        char *envp[] = { 0 };
        execve(path, argv, envp);

        printf("bash: %s: command not found\n", argv[0]);
        exit(127);
    }

    if (pid < 0)
    {
        printf("bash: fork failed\n");
        return;
    }

    for (;;)
    {
        int status = 0;
        pid_t r = waitpid(pid, &status, 0);

        if (r < 0 && errno == EINTR)
        {
            // Ctrl+C while the program was running: stop it and wait
            kill(pid, 2);
            continue;
        }

        if (r < 0)
        {
            printf("bash: waitpid error\n");
            return;
        }

        if (WIFEXITED(status))
        {
            int code = WEXITSTATUS(status);
            if (code != 0)
                printf("[exited with %d]\n", code);
        }
        else if (WIFSIGNALED(status))
        {
            printf("terminated by signal %d\n", WTERMSIG(status));
        }

        return;
    }
}

int main(int argc, char **argv)
{
    unsigned vt = 0;

    if (argc > 1)
    {
        vt = (unsigned)atoi(argv[1]);
        if (vt >= 4)
            vt = 0;
    }

    // bind this shell to its virtual terminal
    dynt_syscall1(DYNT_SYS_VTSET, (long)vt);

    printf("Dynt bash on terminal %u - Ctrl+Alt+F1..F4 to switch\n", vt);

    char line[512];

    for (;;)
    {
        printf("bash: ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            // Ctrl+C at the prompt (or EOF): start a fresh line
            if (errno == EINTR)
            {
                printf("\n");
                continue;
            }
            printf("\n");
            continue;
        }

        line[strcspn(line, "\n")] = 0;

        char *args[32];
        int n = 0;
        char *tok = strtok(line, " ");

        while (tok && n < 30)
        {
            args[n++] = tok;
            tok = strtok(0, " ");
        }
        args[n] = 0;

        if (n == 0)
            continue;

        if (!strcmp(args[0], "help"))
        {
            print_help();
        }
        else if (!strcmp(args[0], "clear"))
        {
            printf("\x1b[2J\x1b[H");
        }
        else if (!strcmp(args[0], "exit"))
        {
            printf("bash: bye\n");
            break;
        }
        else
        {
            run_program(args);
        }
    }

    return 0;
}
