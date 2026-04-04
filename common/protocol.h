#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
 * protocol.h — shared packet definition for both DOS and Windows sides.
 *
 * Every packet sent over the serial link has this structure:
 *
 *   [STX][LEN_HI][LEN_LO][TYPE][PAYLOAD...][CHECKSUM][ETX]
 *
 *   STX      - Start of packet marker (0x02). Receiver looks for this to
 *              know a packet is beginning.
 *   LEN_HI/LO- 16-bit payload length, big-endian (high byte first).
 *              Tells the receiver how many payload bytes to expect.
 *   TYPE     - What kind of packet this is (see PKT_TYPE_* below).
 *   PAYLOAD  - The actual data, up to MAX_PAYLOAD bytes.
 *   CHECKSUM - XOR of all payload bytes. Used to detect corruption.
 *   ETX      - End of packet marker (0x03). Confirms packet is complete.
 *
 * Both sender (Windows) and receiver (DOS) include this header so they
 * speak the same language.
 */

#define STX  0x02   /* Start of packet */
#define ETX  0x03   /* End of packet   */

/* Packet types — tells the receiver what to do with the payload */
#define PKT_TYPE_DATA   0x01  /* Contains a chunk of file data      */
#define PKT_TYPE_ACK    0x02  /* Acknowledgement — "got it, send more" */
#define PKT_TYPE_NAK    0x03  /* Negative ack — "bad packet, resend"   */
#define PKT_TYPE_EOF    0x04  /* End of file — transfer is complete    */
#define PKT_TYPE_HEADER 0x05  /* First packet — contains filename and file size */

/* Maximum bytes of payload per packet.
 * Larger = fewer round trips = faster transfer.
 * 512 bytes is a good balance for serial at 9600-115200 baud. */
#define MAX_PAYLOAD 512

/* In-memory representation of a packet.
 * This is what the code works with — encode/decode converts
 * between this struct and the raw bytes sent over the wire. */
typedef struct {
    unsigned char  type;              /* PKT_TYPE_* constant         */
    unsigned short len;               /* number of bytes in payload  */
    unsigned char  payload[MAX_PAYLOAD]; /* the actual data          */
    unsigned char  checksum;          /* XOR checksum of payload     */
} Packet;

/* Function prototypes — implemented in protocol.c */
unsigned char calc_checksum(const unsigned char *data, unsigned short len);
int  packet_encode(const Packet *pkt, unsigned char *buf, int buf_size);
int  packet_decode(const unsigned char *buf, int buf_len, Packet *pkt);

#endif /* PROTOCOL_H */
