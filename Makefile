CFLAGS = -Wall -Werror -pthread -O3
LDFLAGS = $(LIBS)
LIBS = -lpthread -lgd
WEBDIR = /web/rkeene/projects/webdraw/

serv: serv.o
serv.o: serv.c

# Win32 version, hack.
serv.exe: serv.c win32-pthread-emul.h
	i586-mingw32msvc-gcc -mno-cygwin -Wall -Werror -O3 -I/home/rkeene/root/windows-i386/include $^ -L/home/rkeene/root/windows-i386/lib -static -lgd -lpng -lz -lws2_32 -o $@

.PHONY: clean distclean put-web
put-web:
	$(MAKE) clean serv.exe
	rm -f *.o
	cp * "$(WEBDIR)/"
	rm -f "$(WEBDIR)/HEADER"

clean:
	rm -f serv serv.exe *.o

distclean: clean
