CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -g
LDFLAGS = -pthread -lrt

SRCS = main.c argument_parsing.c file_issues.c shared_memory.c \
       mutex_semaphore.c utils.c word_carrier_process.c \
       letter_carrier_process.c sorting_process.c elevator_process.c

OBJS = $(SRCS:.c=.o)
TARGET = hw3

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean