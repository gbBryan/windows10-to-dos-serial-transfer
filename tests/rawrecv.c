#include <stdio.h>
#include <dos.h>

#define COM1_BASE 0x3F8
#define UART_RBR  0
#define UART_THR  0
#define UART_IER  1
#define UART_LCR  3
#define UART_MCR  4
#define UART_LSR  5
#define UART_DLL  0
#define UART_DLH  1
#define LSR_DR    0x01

int main(void)
{
    unsigned int base = COM1_BASE;
    unsigned int divisor = 115200 / 9600;
    unsigned char ch;
    long i;

    /* Init UART at 9600 8N1 */
    outp(base + UART_LCR, 0x80);
    outp(base + UART_DLL, divisor & 0xFF);
    outp(base + UART_DLH, (divisor >> 8) & 0xFF);
    outp(base + UART_LCR, 0x03);
    outp(base + UART_IER, 0x00);
    outp(base + UART_MCR, 0x03);

    printf("Waiting for data on COM1...\r\n");
    printf("Press Ctrl+C to quit.\r\n");

    /* Read and print forever - no timeout */
    while (1) {
        if (inp(base + UART_LSR) & LSR_DR) {
            ch = (unsigned char)inp(base + UART_RBR);
            if (ch == '\r') continue;
            if (ch == '\n') {
                printf("\r\n");
            } else {
                putchar(ch);
            }
        }
    }

    return 0;
}
