CFLAGS = -Wall -Werror -pthread -O3
LDFLAGS = $(LIBS)
LIBS = -lpthread -lgd
WEBDIR = /web/rkeene/projects/webdraw/

serv: serv.o
serv.o: serv.c

# Win32 version, hack.
serv.exe: serv.c
	i586-mingw32msvc-gcc -Wall -Werror -g3 -I/home/rkeene/root/windows-i386/include $^ -L/home/rkeene/root/windows-i386/lib -static -lgd -lpng -lz -lws2_32 -o $@

.PHONY: clean put-web
put-web:
	$(MAKE) clean serv.exe
	rm -f *.o
	cp * $(WEBDIR)/

clean:
	rm -f serv serv.exe *.o
