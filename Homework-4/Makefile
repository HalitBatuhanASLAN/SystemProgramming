CC       = gcc
CFLAGS   = -Wall -Wextra -pthread -O2 -g
LDFLAGS  = -pthread

TARGET   = analyzer

SRCS     = main.c       \
           shm.c        \
           reader.c     \
           dispatcher.c \
           analyzer.c   \
           aggregator.c \
           watchdog.c

OBJS     = $(SRCS:.c=.o)

HEADERS  = shm.h reader.h dispatcher.h analyzer.h aggregator.h watchdog.h

.PHONY: all clean run valgrind tsan

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) results.txt results.bin results.bin.tmp

run: $(TARGET)
	./$(TARGET) -c services.conf -f priority.txt -k "error,fail,timeout" \
	            -t 2 -w 2 -a 32 -b 32 -d 8 -T 5 \
	            -o results.txt -O results.bin

valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 \
	         ./$(TARGET) -c services.conf -f priority.txt -k "error" \
	                     -t 2 -w 2 -a 16 -b 16 -d 8 -T 5 \
	                     -o results.txt -O results.bin

tsan: CFLAGS += -fsanitize=thread
tsan: LDFLAGS += -fsanitize=thread
tsan: clean $(TARGET)