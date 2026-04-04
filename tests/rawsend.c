#include <windows.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    HANDLE h;
    DCB dcb;
    COMMTIMEOUTS ct;
    char path[32];
    const char *msg = "HELLO FROM WIN10\r\n";
    DWORD written;
    int i;

    if (argc != 2) {
        fprintf(stderr, "Usage: rawsend <COMx>\n");
        return 1;
    }

    snprintf(path, sizeof(path), "\\\\.\\%s", argv[1]);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot open %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = 9600;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    SetCommState(h, &dcb);

    memset(&ct, 0, sizeof(ct));
    ct.WriteTotalTimeoutConstant = 2000;
    SetCommTimeouts(h, &ct);

    for (i = 0; i < 5; i++) {
        WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
        printf("Sent %lu bytes (pass %d)\n", written, i + 1);
        Sleep(500);
    }

    CloseHandle(h);
    return 0;
}
