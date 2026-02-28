AS      = nasm
CC      = gcc -m32
CXX     = g++ -m32

CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -Iinclude
LDFLAGS  = -T linker.ld -ffreestanding -O2 -nostdlib -lgcc

BUILD_DIR = build
ISO_DIR   = isodir

OBJS = \
    $(BUILD_DIR)/boot.o      \
    $(BUILD_DIR)/cpu.o       \
    $(BUILD_DIR)/kstring.o   \
    $(BUILD_DIR)/gdt.o       \
    $(BUILD_DIR)/idt.o       \
    $(BUILD_DIR)/pmm.o       \
    $(BUILD_DIR)/heap.o      \
    $(BUILD_DIR)/pit.o       \
    $(BUILD_DIR)/keyboard.o  \
    $(BUILD_DIR)/ata.o       \
    $(BUILD_DIR)/vfs.o       \
    $(BUILD_DIR)/process.o   \
    $(BUILD_DIR)/nodev.o     \
    $(BUILD_DIR)/vi.o        \
    $(BUILD_DIR)/power.o     \
    $(BUILD_DIR)/shell.o     \
    $(BUILD_DIR)/kernel.o

BIN     = $(BUILD_DIR)/nodos.bin
ISO     = nodos.iso
HEADERS = $(wildcard include/*.h)

.PHONY: all clean run dirs

all: $(ISO) disk.img

dirs:
	mkdir -p $(BUILD_DIR)

disk.img:
	qemu-img create -f raw disk.img 2G

$(BUILD_DIR)/boot.o: boot/boot.asm | dirs
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/cpu.o: boot/cpu.asm | dirs
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/%.o: kernel/%.cpp $(HEADERS) | dirs
	$(CXX) -c $< -o $@ $(CXXFLAGS)

$(BIN): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(ISO): $(BIN) config/grub.cfg
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(BIN)          $(ISO_DIR)/boot/nodos.bin
	cp config/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)

run: $(ISO) disk.img
	@echo ">> QEMU  |  Ctrl+A X = quit  |  Ctrl+Alt+G = release mouse/keyboard"
	env -i PATH=/usr/bin:/bin HOME=$(HOME) DISPLAY=$(DISPLAY) XAUTHORITY=$(XAUTHORITY) \
	qemu-system-i386                     \
	  -cdrom $(ISO)                      \
	  -drive file=disk.img,format=raw,index=0,media=disk \
	  -boot d                            \
	  -m 512M                            \
	  -serial mon:stdio                  \
	  -display gtk,grab-on-hover=off,zoom-to-fit=on     

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO)