#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("Enter your name: ");
    char name[50];

    if (fgets(name, sizeof(name), stdin) != NULL) {
        printf("Hallo, %s", name);
    }

    return 0;
}
