#ifndef SERIAL_H
#define SERIAL_H

/* Platform-agnostic serial port abstraction.
   Implementations live in sender/serial_win32.c and receiver/serial_dos.c */

typedef void *SerialPort;

SerialPort serial_open(const char *port, int baud);
void       serial_close(SerialPort sp);
int        serial_write(SerialPort sp, const unsigned char *buf, int len);
int        serial_read(SerialPort sp, unsigned char *buf, int len, int timeout_ms);

#endif /* SERIAL_H */
