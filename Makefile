CFLAGS = -Wall -Werror -pthread -g3
LDFLAGS = $(LIBS)
LIBS = -lpthread -lgd

serv: serv.o
serv.o: serv.c

# Win32 version, hack.
serv.exe: serv.c
	i586-mingw32msvc-gcc -Wall -Werror -g3 -I/home/rkeene/root/windows-i386/include $^ -L/home/rkeene/root/windows-i386/lib -static -lgd -lpng -lz -lws2_32 -o $@

.PHONY: clean
clean:
	rm -f serv *.o