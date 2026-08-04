#include <stdio.h>

int main(void) {
    char name[50];
    printf("Gib deinen Namen ein: ");

    // Einlesen eines Strings mit fgets
    if (fgets(name, sizeof(name), stdin) != NULL) {
        printf("Hallo, %s", name);
    }

    return 0;
}