# Define compilation toolchain
CC	:= gcc
CFLAGS	:= -Wall -Wextra -Werror
obj := \
	disk.o \
	fs.o

# Target library
lib := libfs.a

all: $(lib)

# Rule for compiling libfs.a
libfs.a: $(obj)
	ar rcs $@ $^

# Compile the object files
%.o: %.c 
	$(CC) -c -o $@ $<

# Clean the object files
clean:
	rm -rf $(obj) $(lib)