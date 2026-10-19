CC = gcc
LD = ld
OBJCOPY = objcopy

CFLAGS = -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -fno-stack-protector \
         -fno-builtin -fno-asynchronous-unwind-tables -fcf-protection=none \
         -Wall -Wextra -Os -march=i386 -mno-sse -mno-mmx -mno-80387 -Isrc
LDFLAGS = -m elf_i386 -nostdlib
KCFLAGS = -m64 -ffreestanding -nostdlib -fno-pie -fno-pic -fno-stack-protector \
          -fno-builtin -fno-asynchronous-unwind-tables -mno-red-zone \
          -mno-sse -mno-mmx -mno-80387 -mcmodel=small -Wall -Wextra -Os

CSRC = $(wildcard src/*.c)
COBJ = $(patsubst src/%.c,build/%.o,$(CSRC))

all: build/disk.img

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/stage1.o: src/stage1.S | build
	$(CC) -m32 -c $< -o $@

build/stage2_entry.o: src/stage2_entry.S | build
	$(CC) -m32 -c $< -o $@

build/bios.o: src/bios.S | build
	$(CC) -m32 -c $< -o $@

build/boot.bin: build/stage1.o
	$(LD) $(LDFLAGS) -Ttext 0x7c00 -e _start --oformat binary -o $@ $<

build/stage2.elf: build/stage2_entry.o build/bios.o $(COBJ) linker.ld
	$(LD) $(LDFLAGS) --no-warn-rwx-segments -T linker.ld -o $@ build/stage2_entry.o build/bios.o $(COBJ)

build/stage2.bin: build/stage2.elf
	$(OBJCOPY) -O binary $< $@

build/kernel_entry.o: example/kernel_entry.S | build
	$(CC) -m64 -c $< -o $@

build/kernel.o: example/kernel.c | build
	$(CC) $(KCFLAGS) -c $< -o $@

build/kernel.elf: build/kernel_entry.o build/kernel.o example/kernel.ld
	$(LD) -m elf_x86_64 --no-warn-rwx-segments -T example/kernel.ld -o $@ build/kernel_entry.o build/kernel.o

build/disk.img: build/boot.bin build/stage2.bin build/kernel.elf example/boot.cfg
	python3 mkimage.py build/boot.bin build/stage2.bin build/disk.img build/kernel.elf example/boot.cfg

run: build/disk.img
	qemu-system-i386 -drive format=raw,file=build/disk.img -m 128 -serial stdio

clean:
	rm -rf build

.PHONY: all run clean
