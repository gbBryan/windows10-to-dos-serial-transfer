#include "../common/serial.h"
#include <dos.h>   /* MK_FP, outp, inp                    */
#include <i86.h>   /* union REGS, int86 for BIOS calls    */
#include <stdlib.h>

/*
 * serial_dos.c — DOS serial port implementation using direct UART access.
 *
 * On DOS there is no operating system API for serial ports like there is
 * on Windows. Instead we talk directly to the UART hardware chip (8250/16550)
 * by reading and writing I/O ports at known addresses.
 *
 * A UART (Universal Asynchronous Receiver/Transmitter) is the chip that
 * converts parallel bytes into serial bits for transmission and vice versa.
 * Every PC has one built in for each COM port.
 *
 * COM port base I/O addresses are standardised across all PC hardware:
 *   COM1 = 0x3F8
 *   COM2 = 0x2F8
 * These have been the same since the original IBM PC in 1981.
 *
 * outp(port, value) — write a byte to an I/O port
 * inp(port)        — read a byte from an I/O port
 */

/* Base I/O addresses for COM1 and COM2 */
#define COM1_BASE 0x3F8
#define COM2_BASE 0x2F8

/*
 * UART register offsets from the base address.
 * Add these to COM1_BASE or COM2_BASE to get the actual port address.
 */
#define UART_THR 0  /* Transmit Holding Register — write byte to send    */
#define UART_RBR 0  /* Receive Buffer Register   — read received byte    */
#define UART_IER 1  /* Interrupt Enable Register — we disable interrupts */
#define UART_LCR 3  /* Line Control Register     — sets baud, data bits  */
#define UART_MCR 4  /* Modem Control Register    — DTR/RTS signals       */
#define UART_LSR 5  /* Line Status Register      — is data ready to read?*/
#define UART_DLL 0  /* Divisor Latch Low  (only when DLAB=1 in LCR)      */
#define UART_DLH 1  /* Divisor Latch High (only when DLAB=1 in LCR)      */

/* Line Status Register bit flags */
#define LSR_DR   0x01  /* Data Ready    — a byte has been received        */
#define LSR_THRE 0x20  /* TX Holding Register Empty — safe to send a byte */

typedef struct {
    unsigned int base;  /* I/O base address of this COM port */
} DosSerial;

/* Returns the base I/O address for "COM1", "COM2" etc. */
static unsigned int get_base(const char *port)
{
    if (port[3] == '1') return COM1_BASE;
    if (port[3] == '2') return COM2_BASE;
    return COM1_BASE;
}

/*
 * serial_open — initialise the UART at the given baud rate.
 *
 * To set the baud rate we write a divisor value to the UART.
 * The UART's internal clock runs at 115200 Hz, so:
 *   divisor = 115200 / desired_baud
 * For 9600 baud: divisor = 12
 *
 * The DLAB (Divisor Latch Access Bit) in the LCR register must be
 * set to 1 before writing the divisor, then cleared back to 0.
 * This is how the UART knows whether you're writing the divisor
 * or normal data to those same register addresses.
 */
SerialPort serial_open(const char *port, long baud)
{
    DosSerial *s;
    unsigned int base;
    unsigned int divisor;

    base    = get_base(port);
    divisor = 115200 / baud;

    outp(base + UART_LCR, 0x80);             /* set DLAB=1 to access divisor */
    outp(base + UART_DLL, divisor & 0xFF);   /* low byte of divisor          */
    outp(base + UART_DLH, (divisor >> 8) & 0xFF); /* high byte of divisor   */

    outp(base + UART_LCR, 0x03);  /* 8 data bits, no parity, 1 stop bit; DLAB=0 */
    outp(base + UART_IER, 0x00);  /* disable interrupts — we poll instead        */
    outp(base + UART_MCR, 0x03);  /* assert DTR and RTS signals                  */

    s = (DosSerial *)malloc(sizeof(DosSerial));
    s->base = base;
    return (SerialPort)s;
}

void serial_close(SerialPort sp)
{
    free(sp);
}

/*
 * serial_write — send bytes one at a time by polling the UART.
 *
 * Before sending each byte we wait until the TX Holding Register is
 * empty (LSR_THRE bit set), meaning the UART is ready to accept
 * another byte to send. This is called "polling" or "busy waiting".
 */
int serial_write(SerialPort sp, const unsigned char *buf, int len)
{
    DosSerial *s = (DosSerial *)sp;
    int i;
    for (i = 0; i < len; i++) {
        while (!(inp(s->base + UART_LSR) & LSR_THRE))
            ;  /* wait until UART is ready to send */
        outp(s->base + UART_THR, buf[i]);
    }
    return len;
}

/*
 * bios_ticks — read the real-time clock tick counter via BIOS interrupt.
 *
 * The BIOS increments a tick counter ~18.2 times per second regardless
 * of CPU speed. We use this for timeouts so they work correctly whether
 * the CPU is running at 4MHz or 4GHz (or in DOSBox at any emulated speed).
 *
 * INT 1Ah AH=00h is the "Get System Time" BIOS call.
 * It returns: CX = high word of tick count, DX = low word.
 *
 * This is more reliable than the memory pointer approach (MK_FP to
 * 0x0040:0x006C) because it goes through the proper BIOS interface.
 */
static unsigned long bios_ticks(void)
{
    union REGS regs;
    regs.h.ah = 0x00;
    int86(0x1A, &regs, &regs);
    return ((unsigned long)regs.w.cx << 16) | (unsigned long)regs.w.dx;
}

/*
 * serial_read — receive up to len bytes with a timeout.
 *
 * Uses a two-stage timeout strategy:
 *   - First byte: wait up to timeout_ms milliseconds. This is the
 *     "inter-packet gap" — we may wait a while for a new packet to arrive.
 *   - Subsequent bytes: only wait ~220ms between bytes. Once a packet
 *     starts arriving, bytes should come quickly. If there's a long gap
 *     mid-packet, something went wrong and we return what we have so far.
 *
 * Returns the number of bytes actually received (may be less than len).
 *
 * Timeout is measured in BIOS ticks (18.2/sec), not CPU cycles, so it
 * works correctly regardless of emulated CPU speed.
 */
int serial_read(SerialPort sp, unsigned char *buf, int len, int timeout_ms)
{
    DosSerial *s = (DosSerial *)sp;
    int i;
    unsigned long start, ticks_needed, ichar_ticks, t;

    /* Convert milliseconds to ticks (18.2 ticks/sec), round up */
    ticks_needed = (unsigned long)timeout_ms * 182UL / 10000UL + 1UL;
    ichar_ticks  = 4UL;  /* ~220ms — short timeout between bytes within a packet */

    for (i = 0; i < len; i++) {
        t = (i == 0) ? ticks_needed : ichar_ticks;
        start = bios_ticks();
        while (!(inp(s->base + UART_LSR) & LSR_DR)) {
            if ((bios_ticks() - start) >= t) return i;  /* timed out */
        }
        buf[i] = (unsigned char)inp(s->base + UART_RBR);  /* read received byte */
    }
    return len;
}
