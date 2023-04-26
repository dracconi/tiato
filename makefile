.PHONY: all test

CC = gcc
SRC = main.c messaging.c messaging.h error.h

all: a.out
	./a.out

a.out: $(SRC)
	$(CC) $(SRC) -o a.out -Wall -Werror

test: test.out
	./test.out

test.out: $(SRC)
	$(CC) $(SRC) -o test.out -Wall -Werror -DTEST=1
