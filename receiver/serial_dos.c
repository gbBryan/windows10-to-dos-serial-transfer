#include "../common/serial.h"
#include <dos.h>
#include <i86.h>
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

SerialPort serial_open(const char *port, long baud)
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

/* Read tick count via BIOS INT 1Ah AH=00, ~18.2 ticks/sec */
static unsigned long bios_ticks(void)
{
    union REGS regs;
    regs.h.ah = 0x00;
    int86(0x1A, &regs, &regs);
    return ((unsigned long)regs.w.cx << 16) | (unsigned long)regs.w.dx;
}

int serial_read(SerialPort sp, unsigned char *buf, int len, int timeout_ms)
{
    DosSerial *s = (DosSerial *)sp;
    int i;
    unsigned long start, ticks_needed, ichar_ticks, t;

    /* 18.2 ticks/sec, convert ms to ticks (round up by adding 1) */
    ticks_needed = (unsigned long)timeout_ms * 182UL / 10000UL + 1UL;
    ichar_ticks  = 4UL; /* ~220ms inter-character timeout within a packet */

    for (i = 0; i < len; i++) {
        /* First byte uses the full timeout, subsequent bytes use short gap timeout */
        t = (i == 0) ? ticks_needed : ichar_ticks;
        start = bios_ticks();
        while (!(inp(s->base + UART_LSR) & LSR_DR)) {
            if ((bios_ticks() - start) >= t) return i;
        }
        buf[i] = (unsigned char)inp(s->base + UART_RBR);
    }
    return len;
}
