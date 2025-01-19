CC=gcc -Werror -Wall -g 

all: threads.o

threads.o: threads.c
	$(CC) -c -o threads.o threads.c

clean:
	rm -f threads.o
