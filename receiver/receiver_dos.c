#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "../common/protocol.h"
#include "../common/serial.h"

#define BAUD_RATE  9600
#define TIMEOUT_MS 2000
#define BUF_SIZE   (MAX_PAYLOAD + 8)
#define BAR_WIDTH  20

/* Read tick count via BIOS INT 1Ah AH=00, ~18.2 ticks/sec */
static unsigned long bios_ticks(void)
{
    union REGS regs;
    regs.h.ah = 0x00;
    int86(0x1A, &regs, &regs);
    return ((unsigned long)regs.w.cx << 16) | (unsigned long)regs.w.dx;
}

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
    static unsigned char buf[BUF_SIZE];
    static Packet pkt;
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

    fp = fopen(argv[2], "wb");
    if (!fp) {
        printf("Failed to open output file: %s\r\n", argv[2]);
        serial_close(sp);
        return 1;
    }

    printf("Waiting for transfer on %s...\r\n", argv[1]);

    /* Wait for header packet - use longer timeout */
    for (;;) {
        printf("DBG: calling serial_read\r\n");
        n = serial_read(sp, buf, BUF_SIZE, 10000);
        printf("DBG: serial_read returned %d\r\n", n);
        if (n <= 0) continue;
        printf("DBG: buf[0]=%02X len=%d buf[last]=%02X\r\n",
               buf[0], n, buf[n-1]);
        printf("DBG: calling packet_decode\r\n");
        if (packet_decode(buf, n, &pkt) != 0) {
            printf("DBG: decode failed, sending NAK\r\n");
            send_nak(sp);
            continue;
        }
        printf("DBG: pkt.type=%d\r\n", (int)pkt.type);
        if (pkt.type == PKT_TYPE_HEADER) break;
    }

    /* Unpack header: 4 bytes file size + filename */
    file_size  = ((long)pkt.payload[0] << 24)
               | ((long)pkt.payload[1] << 16)
               | ((long)pkt.payload[2] <<  8)
               |  (long)pkt.payload[3];
    strncpy(fname, (char *)&pkt.payload[4], 31);
    fname[31] = '\0';

    printf("DBG: sending ACK for header\r\n");
    send_ack(sp);

    printf("Receiving: %s (%ld bytes)\r\n", fname, file_size);

    bytes_received = 0;
    t_start = bios_ticks();

    for (;;) {
        n = serial_read(sp, buf, BUF_SIZE, TIMEOUT_MS);
        if (n <= 0) continue;

        if (packet_decode(buf, n, &pkt) != 0) {
            send_nak(sp);
            continue;
        }

        if (pkt.type == PKT_TYPE_EOF)
            break;

        if (pkt.type == PKT_TYPE_DATA) {
            fwrite(pkt.payload, 1, pkt.len, fp);
            bytes_received += pkt.len;
            send_ack(sp);
            print_progress(bytes_received, file_size);
        }
    }

    t_end      = bios_ticks();
    elapsed_ticks = t_end - t_start;
    elapsed_sec   = (long)(elapsed_ticks * 10 / 182); /* ticks to seconds */
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
