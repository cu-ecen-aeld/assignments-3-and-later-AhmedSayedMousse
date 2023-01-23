ARCH:=arm64
CROSS_COMPILE:=aarch64-none-linux-gnu-
all: clean writer
writer:
	$(CROSS_COMPILE)gcc -o writer writer.c
clean:
	$(RM) writer.o writer

