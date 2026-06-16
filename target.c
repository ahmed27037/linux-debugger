#include <stdio.h>
int main() {
    int x = 0;
    for (int i = 0; i < 3; i++) {
        x = x + 10;
        printf("x is %d\n", x);
    }
    printf("done\n");
    return 0;
}


