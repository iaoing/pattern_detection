SHELL = /bin/sh

OBJS=example.o pattern.o 
CFLAGS=-Wall -g
CC=g++
INCLUDES=
LIBS=

example:${OBJS}
	${CC} ${CFLAGS} ${INCLUDES}  ${OBJS} -o $@ ${LIBS}

clean:
	-rm -f *.o core *.core *.gch

.cpp.o:
	${CC} ${CFLAGS} ${INCLUDES} -c $<
