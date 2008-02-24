CFLAGS = -Wall -Werror -pthread -g3
LDFLAGS = $(LIBS)
LIBS = -lpthread -lgd


serv: serv.o
serv.o: serv.c

.PHONY: clean
clean:
	rm -f serv *.o
