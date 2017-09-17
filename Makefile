CC=gcc
CFLAGS=-o

yash: main.o
	CC main.o -o yash

main.o: main.c
	CC -c main.c

clean:
	rm *.o yash