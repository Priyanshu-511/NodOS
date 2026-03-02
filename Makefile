AS      = nasm
CC      = gcc -m32
CXX     = g++ -m32

CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -Iinclude
LDFLAGS  = -T linker.ld -ffreestanding -O2 -nostdlib -lgcc

BUILD_DIR = build
ISO_DIR   = isodir

OBJS = \
    $(BUILD_DIR)/boot.o          \
    $(BUILD_DIR)/cpu.o           \
    $(BUILD_DIR)/kstring.o       \
    $(BUILD_DIR)/gdt.o           \
    $(BUILD_DIR)/idt.o           \
    $(BUILD_DIR)/pmm.o           \
    $(BUILD_DIR)/heap.o          \
    $(BUILD_DIR)/pit.o           \
    $(BUILD_DIR)/keyboard.o      \
    $(BUILD_DIR)/ata.o           \
    $(BUILD_DIR)/vfs.o           \
    $(BUILD_DIR)/process.o       \
    $(BUILD_DIR)/nodev.o         \
    $(BUILD_DIR)/vi.o            \
    $(BUILD_DIR)/pager.o         \
    $(BUILD_DIR)/power.o         \
    $(BUILD_DIR)/shell.o         \
    $(BUILD_DIR)/fb.o            \
    $(BUILD_DIR)/mouse.o         \
    $(BUILD_DIR)/wm.o            \
    $(BUILD_DIR)/desktop.o       \
    $(BUILD_DIR)/gui_terminal.o  \
    $(BUILD_DIR)/gui_filemanager.o \
    $(BUILD_DIR)/gui_vi.o        \
    $(BUILD_DIR)/gui_settings.o  \
    $(BUILD_DIR)/settings_config.o \
	$(BUILD_DIR)/splash.o        \
    $(BUILD_DIR)/gui.o           \
    $(BUILD_DIR)/kernel.o

BIN     = $(BUILD_DIR)/nodos.bin
ISO     = nodos.iso
HEADERS = $(wildcard include/*.h)

.PHONY: all clean run run-text dirs

all: $(ISO) disk.img

dirs:
	mkdir -p $(BUILD_DIR)

disk.img:
	qemu-img create -f raw disk.img 4G

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

# GUI mode (default) — requests framebuffer via -vga std + GRUB gfxpayload
run: $(ISO) disk.img
	@echo ">> QEMU GUI | Ctrl+Alt+G = release mouse | Ctrl+A X = quit"
	env -i PATH=/usr/bin:/bin HOME=$(HOME) DISPLAY=$(DISPLAY) XAUTHORITY=$(XAUTHORITY) \
	qemu-system-i386                          \
	  -cdrom $(ISO)                           \
	  -drive file=disk.img,format=raw,index=0,media=disk \
	  -boot d                                 \
	  -m 512M                                 \
	  -vga std                                \
	  -device usb-ehci,id=ehci               \
	  -device usb-tablet,bus=ehci.0          \
	  -serial mon:stdio                       \
	  -display gtk,grab-on-hover=off,zoom-to-fit=on

# Text-only fallback — uses second GRUB menu entry
run-text: $(ISO) disk.img
	@echo ">> QEMU text mode | Ctrl+A X = quit"
	env -i PATH=/usr/bin:/bin HOME=$(HOME) DISPLAY=$(DISPLAY) XAUTHORITY=$(XAUTHORITY) \
	qemu-system-i386                          \
	  -cdrom $(ISO)                           \
	  -drive file=disk.img,format=raw,index=0,media=disk \
	  -boot d                                 \
	  -m 512M                                 \
	  -serial tcp::1234,server,nowait         \
	  -monitor stdio 						  \
	  -display gtk,grab-on-hover=off,zoom-to-fit=on

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO)

clear:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO) disk.img