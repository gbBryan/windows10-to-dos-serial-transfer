# Makefile for windows10-to-dos-serial-transfer
# Requires: gcc (Win32 sender/test), OpenWatcom wcc/wcl (DOS/Win9x targets)

CC_WIN   = gcc
CC_DOS   = wcc
CC_W9X   = wcl386
CFLAGS_WIN = -Wall -Icommon
CFLAGS_DOS = -bt=dos -ms -0 -os -Icommon
CFLAGS_W9X = -bt=nt -l=nt -Icommon

COMMON_SRC = common/protocol.c

.PHONY: all sender receiver_dos receiver_win9x loopback clean

all: sender receiver_win9x loopback

sender: $(COMMON_SRC) sender/sender.c sender/serial_win32.c
	$(CC_WIN) $(CFLAGS_WIN) -o sender/sender.exe \
		$(COMMON_SRC) sender/sender.c sender/serial_win32.c

receiver_dos: $(COMMON_SRC) receiver/receiver_dos.c receiver/serial_dos.c
	$(CC_DOS) $(CFLAGS_DOS) $(COMMON_SRC) receiver/receiver_dos.c receiver/serial_dos.c \
		-fe=receiver/receiver_dos.exe

receiver_win9x: $(COMMON_SRC) receiver/receiver_win9x.c sender/serial_win32.c
	$(CC_W9X) $(CFLAGS_W9X) \
		$(COMMON_SRC) receiver/receiver_win9x.c sender/serial_win32.c \
		-fe=receiver/receiver_win9x.exe

loopback: $(COMMON_SRC) tests/loopback_test.c sender/serial_win32.c
	$(CC_WIN) $(CFLAGS_WIN) -o tests/loopback_test.exe \
		$(COMMON_SRC) tests/loopback_test.c sender/serial_win32.c

clean:
	rm -f sender/sender.exe \
	      receiver/receiver_dos.exe receiver/receiver_win9x.exe \
	      tests/loopback_test.exe
