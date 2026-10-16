#include <stdio.h>
#include <stdlib.h>
long long fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: ./fib number\n");
        return 1;
    }
    int n = atoi(argv[1]);
    printf("%lld\n", fibonacci(n));
    return 0;
}
