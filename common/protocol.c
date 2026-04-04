#include "protocol.h"
#include <string.h>

/*
 * protocol.c — packet encoding and decoding.
 *
 * This file handles converting between the Packet struct (what the
 * rest of the code works with) and raw bytes (what gets sent over
 * the serial wire).
 *
 * Think of it like an envelope: encode() puts the letter in the
 * envelope, decode() takes it out and checks it wasn't damaged.
 */

/*
 * calc_checksum — XOR all bytes of the payload together.
 *
 * XOR checksum is simple: if any byte changes in transit, the
 * checksum won't match and we know the packet was corrupted.
 * It's not as strong as CRC but it's fast and good enough for
 * a clean serial link.
 *
 * Example: payload = {0x01, 0x02, 0x03}
 *   sum = 0x00 ^ 0x01 ^ 0x02 ^ 0x03 = 0x00
 */
unsigned char calc_checksum(const unsigned char *data, unsigned short len)
{
    unsigned char sum = 0;
    unsigned short i;
    for (i = 0; i < len; i++)
        sum ^= data[i];
    return sum;
}

/*
 * packet_encode — pack a Packet struct into raw bytes for sending.
 *
 * Writes into buf[] in this order:
 *   buf[0]         = STX (start marker)
 *   buf[1]         = payload length high byte
 *   buf[2]         = payload length low byte
 *   buf[3]         = packet type
 *   buf[4..4+len]  = payload bytes
 *   buf[4+len]     = checksum
 *   buf[5+len]     = ETX (end marker)
 *
 * Returns the total number of bytes written, or -1 if buf is too small.
 */
int packet_encode(const Packet *pkt, unsigned char *buf, int buf_size)
{
    int needed = 5 + pkt->len + 1;
    if (buf_size < needed + 1) return -1;

    buf[0] = STX;
    buf[1] = (unsigned char)(pkt->len >> 8);   /* high byte of length */
    buf[2] = (unsigned char)(pkt->len & 0xFF); /* low byte of length  */
    buf[3] = pkt->type;
    memcpy(&buf[4], pkt->payload, pkt->len);
    buf[4 + pkt->len] = calc_checksum(pkt->payload, pkt->len);
    buf[5 + pkt->len] = ETX;

    return 6 + pkt->len;
}

/*
 * packet_decode — parse raw bytes back into a Packet struct.
 *
 * Validates the packet before accepting it:
 *   1. Must start with STX
 *   2. Payload length must not exceed MAX_PAYLOAD
 *   3. Must have enough bytes for the full packet
 *   4. Must end with ETX
 *   5. Checksum must match
 *
 * Returns 0 on success, -1 if anything is wrong.
 * The receiver sends a NAK if this returns -1, asking the sender
 * to resend the packet.
 */
int packet_decode(const unsigned char *buf, int buf_len, Packet *pkt)
{
    unsigned short len;
    unsigned char chk;

    if (buf_len < 6)            return -1;  /* too short to be a valid packet  */
    if (buf[0] != STX)          return -1;  /* doesn't start with STX          */

    len = ((unsigned short)buf[1] << 8) | buf[2];  /* reconstruct 16-bit length */
    if (len > MAX_PAYLOAD)      return -1;  /* payload too large                */
    if (buf_len < 6 + (int)len) return -1;  /* not enough bytes received yet    */
    if (buf[5 + len] != ETX)    return -1;  /* doesn't end with ETX             */

    chk = calc_checksum(&buf[4], len);
    if (chk != buf[4 + len])    return -1;  /* checksum mismatch = corruption   */

    pkt->type     = buf[3];
    pkt->len      = len;
    memcpy(pkt->payload, &buf[4], len);
    pkt->checksum = chk;

    return 0;
}
