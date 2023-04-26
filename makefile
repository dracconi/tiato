.PHONY: all test

CC = gcc
SRC = main.c messaging.c messaging.h error.h
LIB = -lpthread

all: a.out
	./a.out

a.out: $(SRC)
	$(CC) $(SRC) -o a.out -Wall -Werror $(LIB)

test: test.out
	./test.out

test.out: $(SRC)
	$(CC) $(SRC) -g -o test.out -Wall -Werror -DTEST=1 $(LIB)
