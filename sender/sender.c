#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../common/protocol.h"
#include "../common/serial.h"

/*
 * sender.c — Windows 10 side of the file transfer.
 *
 * This program runs on Windows and sends a file to the DOS receiver
 * over a serial COM port. It uses the same packet protocol as the
 * DOS receiver so both sides can communicate reliably.
 *
 * Transfer flow:
 *   1. Open the COM port and the file to send
 *   2. Send a HEADER packet with the filename and file size
 *   3. Wait for ACK from DOS receiver
 *   4. Loop reading chunks from the file, sending DATA packets
 *   5. For each DATA packet wait for ACK before sending the next
 *   6. If no ACK received, retry up to MAX_RETRIES times
 *   7. Send EOF packet when the whole file has been sent
 *   8. Print transfer summary
 */

#define BAUD_RATE   9600  /* Must match receiver — both sides must agree    */
#define TIMEOUT_MS  2000  /* Wait up to 2 seconds for an ACK                */
#define MAX_RETRIES 3     /* Give up after 3 failed attempts per packet     */
#define BAR_WIDTH   40    /* Width of the [####  ] progress bar             */

/*
 * send_packet — encode a Packet struct and write it to the serial port.
 * Returns 0 on success, -1 on failure.
 */
static int send_packet(SerialPort sp, Packet *pkt)
{
    unsigned char buf[MAX_PAYLOAD + 8];
    int len = packet_encode(pkt, buf, sizeof(buf));
    if (len < 0) return -1;
    return serial_write(sp, buf, len) == len ? 0 : -1;
}

/*
 * wait_ack — wait for an ACK packet from the DOS receiver.
 *
 * An ACK packet is always exactly 6 bytes:
 *   STX, 0x00, 0x00, PKT_TYPE_ACK, 0x00 (checksum), ETX
 * So we read exactly 6 bytes rather than a larger buffer.
 *
 * Returns 0 if a valid ACK was received, -1 otherwise.
 */
static int wait_ack(SerialPort sp)
{
    unsigned char buf[6];
    Packet pkt;
    int n = serial_read(sp, buf, 6, TIMEOUT_MS);
    if (n <= 0) return -1;
    if (packet_decode(buf, n, &pkt) != 0) return -1;
    return (pkt.type == PKT_TYPE_ACK) ? 0 : -1;
}

/*
 * print_progress — draw a [####   ] XX% progress bar.
 *
 * \r moves the cursor to the start of the line without a newline,
 * so each call overwrites the previous progress display in place.
 */
static void print_progress(long sent, long total)
{
    int i, filled;
    int pct = (int)(sent * 100 / total);
    filled = (int)(sent * BAR_WIDTH / total);

    printf("\r[");
    for (i = 0; i < BAR_WIDTH; i++)
        putchar(i < filled ? '#' : ' ');
    printf("] %d%% (%ld / %ld bytes)  ", pct, sent, total);
    fflush(stdout);
}

/*
 * basename_simple — extract just the filename from a full path.
 *
 * e.g. "C:\Games\PKUNZIP.EXE" -> "PKUNZIP.EXE"
 *
 * We send this to the DOS receiver so it can display the correct
 * filename in its progress output, independent of what the user
 * named the output file on the DOS side.
 */
static const char *basename_simple(const char *path)
{
    const char *p = path;
    const char *last = path;
    while (*p) {
        if (*p == '/' || *p == '\\') last = p + 1;
        p++;
    }
    return last;
}

int main(int argc, char *argv[])
{
    SerialPort sp;
    FILE *fp;
    Packet pkt;
    unsigned char chunk[MAX_PAYLOAD];
    size_t n;
    int retries;
    long file_size, bytes_sent;
    clock_t t_start, t_end;
    double elapsed;
    long speed;
    const char *fname;
    unsigned char header[36];  /* 4 bytes size + up to 31 char filename + null */
    int fname_len;

    if (argc != 3) {
        fprintf(stderr, "Usage: sender <COMx> <file>\n");
        return 1;
    }

    sp = serial_open(argv[1], BAUD_RATE);
    if (!sp) {
        fprintf(stderr, "Failed to open %s\n", argv[1]);
        return 1;
    }

    fp = fopen(argv[2], "rb");  /* "rb" = read binary — don't mangle bytes */
    if (!fp) {
        fprintf(stderr, "Failed to open file: %s\n", argv[2]);
        serial_close(sp);
        return 1;
    }

    /* Determine file size by seeking to the end, noting position, seeking back */
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fname = basename_simple(argv[2]);
    fname_len = (int)strlen(fname);
    if (fname_len > 31) fname_len = 31;  /* cap at 31 chars for DOS 8.3 safety */

    /*
     * Build the HEADER packet payload:
     *   bytes 0-3: file size as 32-bit big-endian integer
     *   bytes 4+ : filename string
     *
     * Big-endian (most significant byte first) is standard for
     * network/protocol byte order and works the same on both x86 and
     * other architectures.
     */
    header[0] = (unsigned char)((file_size >> 24) & 0xFF);
    header[1] = (unsigned char)((file_size >> 16) & 0xFF);
    header[2] = (unsigned char)((file_size >>  8) & 0xFF);
    header[3] = (unsigned char)( file_size        & 0xFF);
    memcpy(&header[4], fname, fname_len);
    header[4 + fname_len] = '\0';

    pkt.type = PKT_TYPE_HEADER;
    pkt.len  = (unsigned short)(5 + fname_len);
    memcpy(pkt.payload, header, pkt.len);

    printf("Sending %s via %s at %d baud (%ld bytes)...\n",
           fname, argv[1], BAUD_RATE, file_size);

    /* Send header and wait for ACK before starting data transfer */
    if (send_packet(sp, &pkt) != 0) {
        fprintf(stderr, "Failed to send header packet.\n");
        fclose(fp); serial_close(sp); return 1;
    }
    if (wait_ack(sp) != 0) {
        fprintf(stderr, "No ACK received for header.\n");
        fclose(fp); serial_close(sp); return 1;
    }

    bytes_sent = 0;
    t_start = clock();

    /*
     * Main send loop — read chunks from the file and send as DATA packets.
     *
     * For each packet we wait for an ACK before sending the next one.
     * This is called a "stop and wait" protocol — simple but reliable.
     * If no ACK arrives we retry up to MAX_RETRIES times before giving up.
     */
    while ((n = fread(chunk, 1, MAX_PAYLOAD, fp)) > 0) {
        pkt.type = PKT_TYPE_DATA;
        pkt.len  = (unsigned short)n;
        memcpy(pkt.payload, chunk, n);

        for (retries = 0; retries < MAX_RETRIES; retries++) {
            if (send_packet(sp, &pkt) == 0 && wait_ack(sp) == 0)
                break;  /* success — move on to next chunk */
            fprintf(stderr, "\nRetry %d...", retries + 1);
        }

        if (retries == MAX_RETRIES) {
            fprintf(stderr, "\nTransfer failed after %d retries.\n", MAX_RETRIES);
            fclose(fp); serial_close(sp); return 1;
        }

        bytes_sent += (long)n;
        print_progress(bytes_sent, file_size);
    }

    /* Tell the receiver there's nothing more to send */
    pkt.type = PKT_TYPE_EOF;
    pkt.len  = 0;
    send_packet(sp, &pkt);

    t_end   = clock();
    elapsed = (double)(t_end - t_start) / CLOCKS_PER_SEC;
    speed   = elapsed > 0 ? (long)(bytes_sent / elapsed) : 0;

    printf("\n\nTransfer complete.\n");
    printf("File  : %s\n", fname);
    printf("Size  : %ld bytes\n", file_size);
    printf("Time  : %dm %02ds\n", (int)(elapsed / 60), (int)elapsed % 60);
    printf("Speed : %ld bytes/sec\n", speed);

    fclose(fp);
    serial_close(sp);
    return 0;
}
