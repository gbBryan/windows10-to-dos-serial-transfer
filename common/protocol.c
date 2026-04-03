#include "protocol.h"
#include <string.h>

unsigned char calc_checksum(const unsigned char *data, unsigned short len)
{
    unsigned char sum = 0;
    unsigned short i;
    for (i = 0; i < len; i++)
        sum ^= data[i];
    return sum;
}

int packet_encode(const Packet *pkt, unsigned char *buf, int buf_size)
{
    int needed = 5 + pkt->len + 1; /* STX LEN_HI LEN_LO TYPE payload CHECKSUM ETX */
    int i;
    if (buf_size < needed + 1) return -1;

    buf[0] = STX;
    buf[1] = (unsigned char)(pkt->len >> 8);
    buf[2] = (unsigned char)(pkt->len & 0xFF);
    buf[3] = pkt->type;
    memcpy(&buf[4], pkt->payload, pkt->len);
    buf[4 + pkt->len] = calc_checksum(pkt->payload, pkt->len);
    buf[5 + pkt->len] = ETX;

    return 6 + pkt->len;
}

int packet_decode(const unsigned char *buf, int buf_len, Packet *pkt)
{
    unsigned short len;
    unsigned char chk;

    if (buf_len < 6) return -1;
    if (buf[0] != STX) return -1;

    len = ((unsigned short)buf[1] << 8) | buf[2];
    if (len > MAX_PAYLOAD) return -1;
    if (buf_len < 6 + (int)len) return -1;
    if (buf[5 + len] != ETX) return -1;

    chk = calc_checksum(&buf[4], len);
    if (chk != buf[4 + len]) return -1;

    pkt->type = buf[3];
    pkt->len  = len;
    memcpy(pkt->payload, &buf[4], len);
    pkt->checksum = chk;

    return 0;
}
