#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../common/protocol.h"
#include "../common/serial.h"

#define BAUD_RATE  9600
#define TIMEOUT_MS 2000
#define MAX_RETRIES 3
#define BAR_WIDTH   40

static int send_packet(SerialPort sp, Packet *pkt)
{
    unsigned char buf[MAX_PAYLOAD + 8];
    int len = packet_encode(pkt, buf, sizeof(buf));
    if (len < 0) return -1;
    return serial_write(sp, buf, len) == len ? 0 : -1;
}

static int wait_ack(SerialPort sp)
{
    unsigned char buf[16];
    Packet pkt;
    int n, i;
    n = serial_read(sp, buf, 6, TIMEOUT_MS);
    printf("DBG wait_ack: n=%d\n", n);
    for (i = 0; i < n; i++) printf("  buf[%d]=0x%02X\n", i, buf[i]);
    if (n <= 0) return -1;
    if (packet_decode(buf, n, &pkt) != 0) { printf("DBG: decode failed\n"); return -1; }
    printf("DBG: pkt.type=%d\n", pkt.type);
    return (pkt.type == PKT_TYPE_ACK) ? 0 : -1;
}

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

/* Extract just the filename from a full path */
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
    unsigned char header[36]; /* 4 bytes size + up to 31 bytes name + null */
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

    fp = fopen(argv[2], "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open file: %s\n", argv[2]);
        serial_close(sp);
        return 1;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fname = basename_simple(argv[2]);
    fname_len = (int)strlen(fname);
    if (fname_len > 31) fname_len = 31;

    /* Build header payload: [size 4 bytes big-endian][filename] */
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

    if (send_packet(sp, &pkt) != 0) {
        fprintf(stderr, "Failed to send header packet.\n");
        fclose(fp);
        serial_close(sp);
        return 1;
    }
    printf("Header packet sent, waiting for ACK...\n");
    if (wait_ack(sp) != 0) {
        fprintf(stderr, "No ACK received for header.\n");
        fclose(fp);
        serial_close(sp);
        return 1;
    }

    bytes_sent = 0;
    t_start = clock();

    while ((n = fread(chunk, 1, MAX_PAYLOAD, fp)) > 0) {
        pkt.type = PKT_TYPE_DATA;
        pkt.len  = (unsigned short)n;
        memcpy(pkt.payload, chunk, n);

        for (retries = 0; retries < MAX_RETRIES; retries++) {
            if (send_packet(sp, &pkt) == 0 && wait_ack(sp) == 0)
                break;
            fprintf(stderr, "\nRetry %d...", retries + 1);
        }

        if (retries == MAX_RETRIES) {
            fprintf(stderr, "\nTransfer failed after %d retries.\n", MAX_RETRIES);
            fclose(fp);
            serial_close(sp);
            return 1;
        }

        bytes_sent += (long)n;
        print_progress(bytes_sent, file_size);
    }

    /* Send EOF packet */
    pkt.type = PKT_TYPE_EOF;
    pkt.len  = 0;
    send_packet(sp, &pkt);

    t_end = clock();
    elapsed = (double)(t_end - t_start) / CLOCKS_PER_SEC;
    speed = elapsed > 0 ? (long)(bytes_sent / elapsed) : 0;

    printf("\n\nTransfer complete.\n");
    printf("File  : %s\n", fname);
    printf("Size  : %ld bytes\n", file_size);
    printf("Time  : %dm %02ds\n", (int)(elapsed / 60), (int)elapsed % 60);
    printf("Speed : %ld bytes/sec\n", speed);

    fclose(fp);
    serial_close(sp);
    return 0;
}
