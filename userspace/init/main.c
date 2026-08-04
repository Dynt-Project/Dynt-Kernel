#include <stdio.h>
#include <unistd.h>

int main(void) {
    char name[50];
    printf("Gib deinen Namen ein: ");

    if (fgets(name, sizeof(name), stdin) != NULL) {
        printf("Hallo, %s", name);
    }

    return 0;
}
