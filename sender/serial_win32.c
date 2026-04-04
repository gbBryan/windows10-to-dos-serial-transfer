#include "../common/serial.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * serial_win32.c — Windows serial port implementation using the Win32 API.
 *
 * On Windows, serial ports are accessed like files — you open them with
 * CreateFile(), configure them with SetCommState(), and read/write with
 * ReadFile()/WriteFile(). The OS abstracts away the UART hardware.
 *
 * This is the Windows 10 sender side of the transfer. The DOS receiver
 * (serial_dos.c) does the same job but talks directly to the hardware.
 *
 * COM ports above COM9 require the \\.\COMxx path format — we always
 * use this format so COM5, COM10, etc. all work the same way.
 */

typedef struct {
    HANDLE h;  /* Windows file handle to the COM port */
} Win32Serial;

/*
 * serial_open — open and configure a COM port.
 *
 * DCB (Device Control Block) is the Windows structure that holds
 * serial port settings: baud rate, data bits, parity, stop bits.
 * This is the Win32 equivalent of writing divisor values to the
 * UART registers directly (as serial_dos.c does on the DOS side).
 *
 * 8N1 means: 8 data bits, No parity, 1 stop bit.
 * This is the standard setting for PC serial communication.
 */
SerialPort serial_open(const char *port, long baud)
{
    Win32Serial *s;
    HANDLE h;
    DCB dcb;
    COMMTIMEOUTS timeouts;
    char path[32];

    /* Prefix with \\.\  so COM10 and above work correctly */
    snprintf(path, sizeof(path), "\\\\.\\%s", port);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) { CloseHandle(h); return NULL; }

    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = 8;         /* 8 data bits                    */
    dcb.Parity   = NOPARITY;  /* no parity bit                  */
    dcb.StopBits = ONESTOPBIT;/* 1 stop bit                     */
    dcb.fBinary  = TRUE;      /* binary mode — don't mangle bytes */
    dcb.fParity  = FALSE;

    if (!SetCommState(h, &dcb)) { CloseHandle(h); return NULL; }

    /*
     * COMMTIMEOUTS controls how ReadFile behaves:
     *
     * ReadIntervalTimeout = 0 means don't time out based on gaps between
     * bytes. Without this, Windows was returning early mid-packet when
     * there was a small gap between bytes at 9600 baud.
     *
     * ReadTotalTimeoutConstant = 500ms — if no bytes arrive at all within
     * 500ms, ReadFile gives up and returns. This is overridden per-call
     * in serial_read() below.
     */
    timeouts.ReadIntervalTimeout         = 0;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = 500;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant   = 500;
    SetCommTimeouts(h, &timeouts);

    s = (Win32Serial *)malloc(sizeof(Win32Serial));
    s->h = h;
    return (SerialPort)s;
}

void serial_close(SerialPort sp)
{
    Win32Serial *s = (Win32Serial *)sp;
    if (!s) return;
    CloseHandle(s->h);
    free(s);
}

/*
 * serial_write — send bytes over the COM port.
 *
 * After writing, we purge (discard) any bytes in the receive buffer.
 * This is necessary because com0com (the virtual COM pair driver) can
 * leave echoed bytes in the RX buffer, causing the sender to read back
 * its own transmitted bytes instead of the ACK from DOS.
 */
int serial_write(SerialPort sp, const unsigned char *buf, int len)
{
    Win32Serial *s = (Win32Serial *)sp;
    DWORD written = 0;
    if (!WriteFile(s->h, buf, (DWORD)len, &written, NULL)) return -1;
    /* Discard any echoed bytes so we only read genuine responses */
    PurgeComm(s->h, PURGE_RXCLEAR);
    return (int)written;
}

/*
 * serial_read — receive bytes with a configurable timeout.
 *
 * We update ReadTotalTimeoutConstant before each read so the caller
 * can specify different timeouts for different situations (e.g. longer
 * timeout waiting for header ACK, shorter for data ACKs).
 */
int serial_read(SerialPort sp, unsigned char *buf, int len, int timeout_ms)
{
    Win32Serial *s = (Win32Serial *)sp;
    COMMTIMEOUTS ct;
    DWORD nread = 0;

    GetCommTimeouts(s->h, &ct);
    ct.ReadTotalTimeoutConstant = (DWORD)timeout_ms;
    SetCommTimeouts(s->h, &ct);

    if (!ReadFile(s->h, buf, (DWORD)len, &nread, NULL)) return -1;
    return (int)nread;
}
