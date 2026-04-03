#include <stdio.h>
#include <string.h>
#include "../common/protocol.h"
#include "../common/serial.h"

/* Run with a virtual COM port pair (e.g. com0com: COM5 <-> COM6)
   Usage: loopback_test <sender_port> <receiver_port> */

#define BAUD_RATE  9600
#define TIMEOUT_MS 1000

static const char *TEST_MSG = "Hello, DOS!";

int main(int argc, char *argv[])
{
    SerialPort tx, rx;
    Packet out_pkt, in_pkt;
    unsigned char enc[MAX_PAYLOAD + 8];
    unsigned char raw[MAX_PAYLOAD + 8];
    int enc_len, n;

    if (argc != 3) {
        fprintf(stderr, "Usage: loopback_test <TX_port> <RX_port>\n");
        return 1;
    }

    tx = serial_open(argv[1], BAUD_RATE);
    rx = serial_open(argv[2], BAUD_RATE);
    if (!tx || !rx) {
        fprintf(stderr, "Failed to open ports.\n");
        return 1;
    }

    /* Build a DATA packet */
    out_pkt.type = PKT_TYPE_DATA;
    out_pkt.len  = (unsigned short)strlen(TEST_MSG);
    memcpy(out_pkt.payload, TEST_MSG, out_pkt.len);

    enc_len = packet_encode(&out_pkt, enc, sizeof(enc));
    if (enc_len < 0) { fprintf(stderr, "Encode failed\n"); return 1; }

    serial_write(tx, enc, enc_len);

    n = serial_read(rx, raw, sizeof(raw), TIMEOUT_MS);
    if (n <= 0) { fprintf(stderr, "No data received\n"); return 1; }

    if (packet_decode(raw, n, &in_pkt) != 0) {
        fprintf(stderr, "Decode failed\n");
        return 1;
    }

    if (in_pkt.len != out_pkt.len ||
        memcmp(in_pkt.payload, out_pkt.payload, in_pkt.len) != 0) {
        fprintf(stderr, "Data mismatch!\n");
        return 1;
    }

    printf("Loopback test passed: \"%.*s\"\n", (int)in_pkt.len, in_pkt.payload);

    serial_close(tx);
    serial_close(rx);
    return 0;
}
