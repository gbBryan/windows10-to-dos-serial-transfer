#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/protocol.h"
#include "../common/serial.h"

#define BAUD_RATE 9600
#define TIMEOUT_MS 2000
#define MAX_RETRIES 3

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
    int n = serial_read(sp, buf, sizeof(buf), TIMEOUT_MS);
    if (n <= 0) return -1;
    if (packet_decode(buf, n, &pkt) != 0) return -1;
    return (pkt.type == PKT_TYPE_ACK) ? 0 : -1;
}

int main(int argc, char *argv[])
{
    SerialPort sp;
    FILE *fp;
    Packet pkt;
    unsigned char chunk[MAX_PAYLOAD];
    size_t n;
    int retries;

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

    printf("Sending %s via %s at %d baud...\n", argv[2], argv[1], BAUD_RATE);

    while ((n = fread(chunk, 1, MAX_PAYLOAD, fp)) > 0) {
        pkt.type = PKT_TYPE_DATA;
        pkt.len  = (unsigned short)n;
        memcpy(pkt.payload, chunk, n);

        for (retries = 0; retries < MAX_RETRIES; retries++) {
            if (send_packet(sp, &pkt) == 0 && wait_ack(sp) == 0)
                break;
            fprintf(stderr, "Retry %d...\n", retries + 1);
        }

        if (retries == MAX_RETRIES) {
            fprintf(stderr, "Transfer failed after %d retries.\n", MAX_RETRIES);
            fclose(fp);
            serial_close(sp);
            return 1;
        }
    }

    /* Send EOF packet */
    pkt.type = PKT_TYPE_EOF;
    pkt.len  = 0;
    send_packet(sp, &pkt);

    printf("Transfer complete.\n");
    fclose(fp);
    serial_close(sp);
    return 0;
}
