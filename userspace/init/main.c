#include <stdio.h>
#include <unistd.h>
#include <string.h>

void help_command() {
    printf("avalible commands:\n -help\n -version\n -clear\n");
}

void version_command() {
    printf("Dynt-kernel version 1.0.1\n");
}

void clear_command() {
    printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
}

int main(void) {
    printf("basic stupid shell!\n\n");
    while (1) {
        printf("bash: ");
        char command[500];

        if (fgets(command, sizeof(command), stdin) != NULL) {
            if (strchr(command,'help')!=NULL) {
                help_command();
            }else if (strchr(command,'version')!=NULL) {
                version_command();
            } else if (strchr(command,'clear')!=NULL) {
                clear_command();
            }
        }
    }

        //execve("/bash");
    return 0;
}
