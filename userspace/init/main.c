#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

// starts one bash instance on every virtual terminal (VT 0..3) and then
// babysits them. the vt number is passed to bash as argv[1] so each
// shell knows which terminal it owns. switch terminals with Ctrl+Alt+F1..F4.
#define BASH_COUNT 4

int main(void)
{
    printf("Dynt-Kernel init: starting %d terminals...\n", BASH_COUNT);

    for (int i = 0; i < BASH_COUNT; i++)
    {
        pid_t pid = fork();

        if (pid == 0)
        {
            char num[8];
            snprintf(num, sizeof(num), "%d", i);

            char *argv[3];
            argv[0] = (char *)"/bash";
            argv[1] = num;
            argv[2] = 0;

            char *envp[] = { 0 };
            execve("/bash", argv, envp);

            printf("init: could not start bash on terminal %d\n", i);
            for (;;)
                ;
        }
    }

    // reap any bash that dies, start it again on its terminal
    for (int i = 0; i < BASH_COUNT; i++)
        waitpid(-1, 0, 0);

    return 0;
}
