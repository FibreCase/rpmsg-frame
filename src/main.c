#include <stdio.h>
#include <unistd.h>
#include "add.h"

int main() {
    printf("Hello, World!\n");
    int ret = add(1, 2);
    printf("Result of 1+2 is %d", ret);
    sleep(5);
    scanf("%d", &ret);
    return 0;
}
