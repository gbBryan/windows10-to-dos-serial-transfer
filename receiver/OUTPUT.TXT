#include <stdio.h>

int main(void)
{
    printf("Hello from serial-transfer project!\n");
    printf("Compiled with: ");

#if defined(__WATCOMC__)
    printf("Open Watcom\n");
#elif defined(__GNUC__)
    printf("GCC/MinGW\n");
#else
    printf("Unknown compiler\n");
#endif

    return 0;
}