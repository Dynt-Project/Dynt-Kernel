#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("Enter your name: \n");
    char name[50];

    if (fgets(name, sizeof(name), stdin) != NULL) {
        printf("Hello, %s", name);
    }

    return 0;
}
