#include "../common/serial.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    HANDLE h;
} Win32Serial;

SerialPort serial_open(const char *port, long baud)
{
    Win32Serial *s;
    HANDLE h;
    DCB dcb;
    COMMTIMEOUTS timeouts;
    char path[32];

    /* Support "COM10" and above via \\.\COMxx */
    snprintf(path, sizeof(path), "\\\\.\\%s", port);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) { CloseHandle(h); return NULL; }

    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;

    if (!SetCommState(h, &dcb)) { CloseHandle(h); return NULL; }

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

int serial_write(SerialPort sp, const unsigned char *buf, int len)
{
    Win32Serial *s = (Win32Serial *)sp;
    DWORD written = 0;
    if (!WriteFile(s->h, buf, (DWORD)len, &written, NULL)) return -1;
    return (int)written;
}

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
