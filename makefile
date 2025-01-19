CC = gcc -Werror -Wall -g

TEST_PROGRAM = test_tls
LIBRARY = libtls.a
OBJECTS = tls.o threads.o

all: $(TEST_PROGRAM)

tls.o: tls.c
	$(CC) -c -o tls.o tls.c

threads.o: tls_threads.c
	$(CC) -c -o threads.o tls_threads.c

test_tls.o: test_tls.c tls.h
	$(CC) -c -o test_tls.o test_tls.c

$(TEST_PROGRAM): test_tls.o $(OBJECTS)
	$(CC) -o $(TEST_PROGRAM) test_tls.o $(OBJECTS)

clean:
	rm -f *.o $(TEST_PROGRAM) $(LIBRARY)
