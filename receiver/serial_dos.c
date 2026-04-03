#include "../common/serial.h"
#include <dos.h>
#include <stdlib.h>

/* Direct UART register offsets (base addresses for COM1/COM2) */
#define COM1_BASE 0x3F8
#define COM2_BASE 0x2F8

#define UART_THR 0  /* Transmit Holding Register */
#define UART_RBR 0  /* Receive Buffer Register    */
#define UART_IER 1  /* Interrupt Enable Register  */
#define UART_LCR 3  /* Line Control Register      */
#define UART_MCR 4  /* Modem Control Register     */
#define UART_LSR 5  /* Line Status Register       */
#define UART_DLL 0  /* Divisor Latch Low (DLAB=1) */
#define UART_DLH 1  /* Divisor Latch High (DLAB=1)*/

#define LSR_DR   0x01  /* Data Ready    */
#define LSR_THRE 0x20  /* TX Holding Register Empty */

typedef struct {
    unsigned int base;
} DosSerial;

static unsigned int get_base(const char *port)
{
    if (port[3] == '1') return COM1_BASE;
    if (port[3] == '2') return COM2_BASE;
    return COM1_BASE;
}

SerialPort serial_open(const char *port, int baud)
{
    DosSerial *s;
    unsigned int base;
    unsigned int divisor;

    base    = get_base(port);
    divisor = 115200 / baud;

    /* Set DLAB to access divisor registers */
    outp(base + UART_LCR, 0x80);
    outp(base + UART_DLL, divisor & 0xFF);
    outp(base + UART_DLH, (divisor >> 8) & 0xFF);

    /* 8N1, clear DLAB */
    outp(base + UART_LCR, 0x03);
    outp(base + UART_IER, 0x00); /* Disable interrupts */
    outp(base + UART_MCR, 0x03); /* DTR + RTS */

    s = (DosSerial *)malloc(sizeof(DosSerial));
    s->base = base;
    return (SerialPort)s;
}

void serial_close(SerialPort sp)
{
    free(sp);
}

int serial_write(SerialPort sp, const unsigned char *buf, int len)
{
    DosSerial *s = (DosSerial *)sp;
    int i;
    for (i = 0; i < len; i++) {
        while (!(inp(s->base + UART_LSR) & LSR_THRE))
            ;
        outp(s->base + UART_THR, buf[i]);
    }
    return len;
}

int serial_read(SerialPort sp, unsigned char *buf, int len, int timeout_ms)
{
    DosSerial *s = (DosSerial *)sp;
    int i;
    long deadline;

    for (i = 0; i < len; i++) {
        /* Busy-wait with a crude timeout loop */
        deadline = timeout_ms * 1000L;
        while (!(inp(s->base + UART_LSR) & LSR_DR)) {
            if (--deadline <= 0) return i;
        }
        buf[i] = (unsigned char)inp(s->base + UART_RBR);
    }
    return len;
}
