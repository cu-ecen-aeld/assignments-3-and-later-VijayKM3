# Makefile for Writer as target

CC = $(CROSS_COMPILE)gcc

CFLAGS = -D_GNU_SOURCE -Wall -Wextra -std=c11 -O2

TARGET = aesdsocket

SRCS = aesdsocket.c

all: $(TARGET)

$(TARGET): aesdsocket.o
	$(CC) aesdsocket.o -o $(TARGET)

writer.o: $(SRCS)
	$(CC) $(CFLAGS) -c $(SRCS) -o aesdsocket.o

clean:
	rm -f *.o $(TARGET)


