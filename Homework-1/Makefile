# compiler to use gcc is standard C complier for Posix and Linux 
CC = gcc
# flags for compiletations
CFLAGS = -Wall -Wextra -g

# target executable name
TARGET = HBA_file_search

# source files to compile
SRCS = main.c \
	   check_files.c \
	   parse_argv.c \
	   print_tree.c \
	   regex.c \
	   searching_files.c \
	   stopping_handler.c \

# generate object files from source files ( .o files for all .c diles)
OBJS = $(patsubst %.c, %.o, $(SRCS))

# final target file
all: $(TARGET)

# compiletaion rule of program
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# complie each .c file to .o file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# cleaning all .o files and target executable file
clean:
	rm -f $(OBJS) $(TARGET)

# prevents if some files have names all or clean
.PHONY: all clean