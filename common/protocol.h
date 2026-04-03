#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

/* Packet structure:
   [STX][LEN_HI][LEN_LO][TYPE][PAYLOAD...][CHECKSUM][ETX] */

#define STX  0x02
#define ETX  0x03

#define PKT_TYPE_DATA  0x01
#define PKT_TYPE_ACK   0x02
#define PKT_TYPE_NAK   0x03
#define PKT_TYPE_EOF   0x04

#define MAX_PAYLOAD 128

typedef struct {
    unsigned char  type;
    unsigned short len;
    unsigned char  payload[MAX_PAYLOAD];
    unsigned char  checksum;
} Packet;

unsigned char calc_checksum(const unsigned char *data, unsigned short len);
int  packet_encode(const Packet *pkt, unsigned char *buf, int buf_size);
int  packet_decode(const unsigned char *buf, int buf_len, Packet *pkt);

#endif /* PROTOCOL_H */
