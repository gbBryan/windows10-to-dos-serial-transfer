#include <stdio.h>
#include <string.h>
#include "../common/protocol.h"
#include "../common/serial.h"

#define BAUD_RATE  9600
#define TIMEOUT_MS 2000
#define BUF_SIZE   (MAX_PAYLOAD + 8)

static void send_ack(SerialPort sp)
{
    Packet ack;
    unsigned char buf[16];
    int len;
    ack.type = PKT_TYPE_ACK;
    ack.len  = 0;
    len = packet_encode(&ack, buf, sizeof(buf));
    if (len > 0) serial_write(sp, buf, len);
}

static void send_nak(SerialPort sp)
{
    Packet nak;
    unsigned char buf[16];
    int len;
    nak.type = PKT_TYPE_NAK;
    nak.len  = 0;
    len = packet_encode(&nak, buf, sizeof(buf));
    if (len > 0) serial_write(sp, buf, len);
}

int main(int argc, char *argv[])
{
    SerialPort sp;
    FILE *fp;
    unsigned char buf[BUF_SIZE];
    Packet pkt;
    int n;

    if (argc != 3) {
        printf("Usage: receiver_dos <COM1|COM2> <outfile>\r\n");
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

    printf("Receiving on %s -> %s\r\n", argv[1], argv[2]);

    for (;;) {
        n = serial_read(sp, buf, BUF_SIZE, TIMEOUT_MS);
        if (n <= 0) continue;

        if (packet_decode(buf, n, &pkt) != 0) {
            send_nak(sp);
            continue;
        }

        if (pkt.type == PKT_TYPE_EOF) {
            printf("Transfer complete.\r\n");
            break;
        }

        if (pkt.type == PKT_TYPE_DATA) {
            fwrite(pkt.payload, 1, pkt.len, fp);
            send_ack(sp);
        }
    }

    fclose(fp);
    serial_close(sp);
    return 0;
}
