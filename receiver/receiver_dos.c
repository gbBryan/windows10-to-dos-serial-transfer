#include <stdio.h>
#include <string.h>
#include <dos.h>   /* MK_FP                              */
#include <i86.h>   /* union REGS, int86 for BIOS calls   */
#include "../common/protocol.h"
#include "../common/serial.h"

/*
 * receiver_dos.c — DOS side of the file transfer.
 *
 * This program runs inside DOSBox (or real DOS hardware) and waits for
 * files to be sent from the Windows 10 sender over a serial link.
 *
 * Transfer flow:
 *   1. Wait for a HEADER packet — tells us the filename and file size
 *   2. Send ACK to confirm we got the header
 *   3. Loop receiving DATA packets — write each payload to disk, send ACK
 *   4. When EOF packet arrives, print summary and exit
 *
 * If a packet fails to decode (bad checksum, framing error etc.) we send
 * a NAK and the sender will retransmit that packet.
 */

#define BAUD_RATE  9600   /* Must match sender — both sides must agree  */
#define TIMEOUT_MS 2000   /* How long to wait for a packet (ms)         */
#define BUF_SIZE   (MAX_PAYLOAD + 8)  /* Buffer for raw incoming bytes  */
#define BAR_WIDTH  20     /* Width of the progress bar in characters    */

/*
 * bios_ticks — read the real-time tick counter via BIOS interrupt.
 * See serial_dos.c for full explanation.
 */
static unsigned long bios_ticks(void)
{
    union REGS regs;
    regs.h.ah = 0x00;
    int86(0x1A, &regs, &regs);
    return ((unsigned long)regs.w.cx << 16) | (unsigned long)regs.w.dx;
}

/*
 * send_ack — tell the sender "packet received OK, send the next one".
 * Static buffers avoid putting large objects on the small DOS stack.
 */
static void send_ack(SerialPort sp)
{
    static Packet ack;
    static unsigned char buf[16];
    int len;
    ack.type = PKT_TYPE_ACK;
    ack.len  = 0;
    memset(ack.payload, 0, sizeof(ack.payload));
    len = packet_encode(&ack, buf, sizeof(buf));
    if (len > 0) serial_write(sp, buf, len);
}

/*
 * send_nak — tell the sender "bad packet, please resend".
 */
static void send_nak(SerialPort sp)
{
    static Packet nak;
    static unsigned char buf[16];
    int len;
    nak.type = PKT_TYPE_NAK;
    nak.len  = 0;
    memset(nak.payload, 0, sizeof(nak.payload));
    len = packet_encode(&nak, buf, sizeof(buf));
    if (len > 0) serial_write(sp, buf, len);
}

/*
 * print_progress — draw a [####   ] progress bar on a single line.
 *
 * \r (carriage return without newline) moves the cursor back to the
 * start of the line so each update overwrites the previous one.
 * This is standard technique for console progress displays.
 */
static void print_progress(long received, long total)
{
    int i, filled;
    int pct;

    if (total <= 0) {
        printf("\r%ld bytes received   ", received);
        return;
    }

    pct    = (int)(received * 100 / total);
    filled = (int)(received * BAR_WIDTH / total);

    printf("\r[");
    for (i = 0; i < BAR_WIDTH; i++)
        putchar(i < filled ? '#' : ' ');
    printf("] %d%%  ", pct);
}

int main(int argc, char *argv[])
{
    SerialPort sp;
    FILE *fp;
    static unsigned char buf[BUF_SIZE]; /* static = lives in data segment, not stack */
    static Packet pkt;                  /* Packet has 512-byte payload — too big for stack */
    int n;
    long file_size, bytes_received;
    char fname[32];
    unsigned long t_start, t_end, elapsed_ticks;
    long elapsed_sec, speed;

    if (argc != 3) {
        printf("Usage: recv <COM1|COM2> <outfile>\r\n");
        return 1;
    }

    sp = serial_open(argv[1], BAUD_RATE);
    if (!sp) {
        printf("Failed to open serial port\r\n");
        return 1;
    }

    fp = fopen(argv[2], "wb");  /* "wb" = write binary — don't mangle bytes */
    if (!fp) {
        printf("Failed to open output file: %s\r\n", argv[2]);
        serial_close(sp);
        return 1;
    }

    printf("Waiting for transfer on %s...\r\n", argv[1]);

    /*
     * Wait for the HEADER packet.
     * The header is always the first packet sent and contains the
     * filename and file size so we can show an accurate progress bar.
     * We use a longer timeout here (10 seconds) since the user may
     * take a moment to start the sender after starting the receiver.
     */
    for (;;) {
        n = serial_read(sp, buf, BUF_SIZE, 10000);
        if (n <= 0) continue;
        if (packet_decode(buf, n, &pkt) != 0) { send_nak(sp); continue; }
        if (pkt.type == PKT_TYPE_HEADER) break;
    }

    /*
     * Unpack the header payload:
     *   bytes 0-3: file size as a 32-bit big-endian integer
     *   bytes 4+ : filename as a null-terminated string
     *
     * Big-endian means the most significant byte comes first.
     * We reconstruct the 32-bit value by shifting each byte into place.
     */
    file_size  = ((long)pkt.payload[0] << 24)
               | ((long)pkt.payload[1] << 16)
               | ((long)pkt.payload[2] <<  8)
               |  (long)pkt.payload[3];
    strncpy(fname, (char *)&pkt.payload[4], 31);
    fname[31] = '\0';

    send_ack(sp);

    printf("Receiving: %s (%ld bytes)\r\n", fname, file_size);

    bytes_received = 0;
    t_start = bios_ticks();

    /* Main receive loop — keep going until we get an EOF packet */
    for (;;) {
        n = serial_read(sp, buf, BUF_SIZE, TIMEOUT_MS);
        if (n <= 0) continue;

        if (packet_decode(buf, n, &pkt) != 0) {
            send_nak(sp);   /* bad packet — ask sender to retry */
            continue;
        }

        if (pkt.type == PKT_TYPE_EOF)
            break;  /* sender says we're done */

        if (pkt.type == PKT_TYPE_DATA) {
            fwrite(pkt.payload, 1, pkt.len, fp);  /* write payload bytes to file */
            bytes_received += pkt.len;
            send_ack(sp);
            print_progress(bytes_received, file_size);
        }
    }

    /* Calculate transfer statistics using BIOS ticks */
    t_end         = bios_ticks();
    elapsed_ticks = t_end - t_start;
    elapsed_sec   = (long)(elapsed_ticks * 10 / 182);  /* ticks → seconds */
    speed = elapsed_sec > 0 ? bytes_received / elapsed_sec : 0;

    printf("\r\n\r\nTransfer complete.\r\n");
    printf("File  : %s\r\n", fname);
    printf("Size  : %ld bytes\r\n", bytes_received);
    printf("Time  : %ldm %02lds\r\n", elapsed_sec / 60, elapsed_sec % 60);
    printf("Speed : %ld bytes/sec\r\n", speed);

    fclose(fp);
    serial_close(sp);
    return 0;
}
