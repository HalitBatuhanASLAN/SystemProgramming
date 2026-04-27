# =============================================================================
#  CSE 344 – Homework #4
#  Multi-Process & Multi-Threaded Concurrent Log Analysis Pipeline
#
#  Tüm kaynak dosyalar (.c / .h) bu Makefile ile aynı dizindedir.
#  Derleme: gcc -Wall -Wextra -pthread
#  Hedef  : ./analyzer
# =============================================================================

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

# Tüm .c dosyaları tüm header'lara bağımlı tutmak güvenli ama
# aşırı yeniden derlemeye yol açar. Her .c kendi header'ına ve shm.h'ye
# bağımlı kabul ediliyor.
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) results.txt results.bin results.bin.tmp

# Yardımcı: hızlı test için
run: $(TARGET)
	./$(TARGET) -c services.conf -f priority.txt -k "error,fail,timeout" \
	            -t 2 -w 2 -a 32 -b 32 -d 8 -T 5 \
	            -o results.txt -O results.bin

# Valgrind testi (Test 6)
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 \
	         ./$(TARGET) -c services.conf -f priority.txt -k "error" \
	                     -t 2 -w 2 -a 16 -b 16 -d 8 -T 5 \
	                     -o results.txt -O results.bin

# ThreadSanitizer ile race kontrolü (geliştirme aşaması)
tsan: CFLAGS += -fsanitize=thread
tsan: LDFLAGS += -fsanitize=thread
tsan: clean $(TARGET)