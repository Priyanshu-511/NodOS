# NodOS

```
      _   _           _  ___  ____
     | \ | | ___   __| |/ _ \/ ___|
     |  \| |/ _ \ / _` | | | \___ \
     | |\  | (_) | (_| | |_| |___) |
     |_| \_|\___/ \__,_|\___/|____/

  NodOS v3.0  //  x86 32-bit  //  NodeV Engine  //  256 MB RAM  //  2 GB Disk
```

NodOS is a freestanding x86 (32-bit) operating system built from scratch in C++ and NASM assembly. It boots via GRUB 2 Multiboot, runs in 32-bit protected mode, and provides a fully interactive shell with a built-in scripting language, a modal vi text editor, persistent ATA disk storage, a terminal pager, and proper power management (shutdown + reboot).

---

## Table of Contents

- [What's New in v3.0](#whats-new-in-v30)
- [Features at a Glance](#features-at-a-glance)
- [Quick Start](#quick-start)
- [Persistent Storage](#persistent-storage)
- [Shell Reference](#shell-reference)
- [NodeV Scripting Language](#nodev-scripting-language)
- [vi Text Editor](#vi-text-editor)
- [Power Management](#power-management)
- [Terminal Pager](#terminal-pager)
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Boot Sequence](#boot-sequence)
- [Interrupt Handling](#interrupt-handling)
- [Dependencies](#dependencies)
- [Building](#building)
- [Troubleshooting](#troubleshooting)
- [Extending NodOS](#extending-nodos)

---

## What's New in v3.0

| Feature | Details |
|---------|---------|
| **`shutdown`** | Cleanly halts and powers off the VM via ACPI/QEMU magic ports |
| **`reboot`** | Reboots via the 8042 keyboard controller pulse |
| **`halt`** | Stops the CPU immediately (`cli; hlt`) |
| **`fetch`** | Prints a neofetch-style system info banner on demand |
| **Terminal pager** | Long output auto-pauses every 22 lines with `--More--` |
| **New VFS backend** | Direct ATA sector I/O with in-memory MFT + free-space bitmap |
| **8 MB heap** | Doubled from 4 MB |
| **16 KB kernel stack** | Doubled (`boot.asm` `.bss` stack) |
| **`krealloc`** | Added to heap for dynamic buffer resizing |
| **kstring additions** | `k_strtok`, `k_strchr`, `k_strstr` now available |
| **Coloured boot banner** | Gradient blue → white ASCII art with live disk/RAM stats |
| **Improved `cpu.asm`** | Cleaner ISR/IRQ macros, proper error-code handling for all 32 exceptions |

---

## Features at a Glance

| Component | Description |
|-----------|-------------|
| **Bootloader** | GRUB 2 Multiboot — standard `.iso` boot, no custom bootloader |
| **CPU mode** | x86 32-bit protected mode with GDT (5 descriptors) and IDT (256 gates) |
| **Stack** | 16 KB kernel stack defined in `boot.asm` |
| **Memory** | PMM bitmap over 256 MB; 8 MB kernel heap (`kmalloc`/`kfree`/`krealloc`) |
| **Interrupts** | 32 ISRs (CPU exceptions) + 16 IRQs (hardware), PIC remapped to INT 32–47 |
| **Timer** | PIT at 100 Hz (IRQ 0) — uptime, `pit_sleep`, process wake-up |
| **Keyboard** | PS/2 with shift + caps-lock support (IRQ 1), 256-byte ring buffer |
| **Multitasking** | Co-operative round-robin scheduler, up to 16 processes, 8 KB stacks |
| **ATA storage** | PIO polling, primary bus master — sector-level read/write with cache flush |
| **Filesystem** | VFS with MFT-style on-disk directory + in-memory free-space bitmap |
| **Shell** | 23 built-in commands, coloured prompt, argument tokeniser |
| **Scripting** | NodeV — fully embedded interpreted language (lexer + parser + evaluator) |
| **Text editor** | vi — Normal / Insert / Command modes, full cursor movement |
| **Power** | `shutdown` (ACPI), `reboot` (8042 pulse), `halt` (CLI+HLT) |
| **Pager** | Automatic `--More--` pause every 22 lines for long output |

---

## Quick Start

```bash
# 1. Install build dependencies  (Ubuntu / Debian / Kali)
sudo apt install nasm g++-multilib grub-pc-bin grub-common xorriso mtools qemu-system-x86

# 2. Build the ISO
make

# 3. Create a 2 GB persistent disk image (once) and then Run
make run
```

On first boot you land at the shell. Type `help` to see all commands, or `fetch` to see the system banner again.

---

## Persistent Storage

NodOS stores files directly on a raw disk image using a custom **MFT-style filesystem** — no separate format step is needed. All file data and metadata are written to disk immediately on every operation.

### How it works

The VFS layer manages an **in-memory directory table** (`entries[]`, 64 slots) and an **in-memory free-space bitmap** covering all 4 million sectors of a 2 GB disk. On boot, `vfs_init()` reads the MFT back from disk (LBA 1–127) and reconstructs the bitmap from it. Every write, create, delete, or rename flushes the MFT back to disk via `save_mft()`.

```
nodos_disk.img  (raw 512-byte sectors)
│
├── LBA 0           Reserved (OS metadata)
├── LBA 1 – 127     MFT — 64 × VFSEntry records (flushed on every change)
└── LBA 128+        File data  (contiguous sector runs, first-fit allocation)
```

### VFSEntry on disk

Each directory entry stores:

| Field | Size | Description |
|-------|------|-------------|
| `path` | 128 B | Absolute path, e.g. `/home/notes.txt` |
| `data` | 4096 B | (legacy padding — data lives on disk) |
| `type` | 4 B | `VFS_FILE` or `VFS_DIR` |
| `size` | 4 B | File size in bytes |
| `start_lba` | 4 B | First data sector |
| `sector_count` | 4 B | Number of sectors allocated |
| `used` | 1 B | Slot occupied flag |

### Setup

```bash
# Create the disk image (once)
make disk           # nodos_disk.img, 2 GB default

# For 4 GB: edit DISK_SIZE = 4096 in Makefile first
make run            # boots with disk attached — files persist automatically
```

No `format` command needed. Files are immediately persistent from the first write.

---

## Shell Reference

The prompt always shows your current directory:

```
nodos@kernel:/home$
```

### System information

| Command | Description |
|---------|-------------|
| `help` | List all commands |
| `fetch` | Print the neofetch-style system info banner |
| `info` | OS version, architecture, uptime |
| `mem` | PMM free/used pages, heap used/total |
| `time` | Uptime as HH:MM:SS and raw tick count |
| `clear` | Clear the screen |

### Process management

| Command | Description |
|---------|-------------|
| `ps` | List all processes (PID, name, state) |
| `kill <pid>` | Terminate a process by PID |
| `echo <text>` | Print text to the screen |

### Filesystem navigation

| Command | Description |
|---------|-------------|
| `pwd` | Print working directory |
| `cd <path>` | Change directory (`cd ..` goes up one level) |
| `ls` | List current directory — `[DIR]` cyan, `[FILE]` green with size |
| `ls <path>` | List a specific path |
| `md <dir>` | Create a new directory |
| `rd <dir>` | Remove a directory and all its contents |
| `mv <src> <dst>` | Move or rename a file or directory |
| `cp <src> <dst>` | Copy a file or directory tree |

### File operations

| Command | Description |
|---------|-------------|
| `cat <file>` | Print file contents (pager activates if long) |
| `write <file> <text…>` | Create or overwrite a file with inline text |
| `rm <file>` | Delete a file |
| `vi <file>` | Open file in the built-in vi editor |

### Scripting

| Command | Description |
|---------|-------------|
| `nodev <file>` | Execute a NodeV `.nod` script from the filesystem |

### Power

| Command | Description |
|---------|-------------|
| `shutdown` | Power off the VM via ACPI ports (falls back to `cli; hlt`) |
| `reboot` | Reboot via 8042 keyboard controller reset pulse |
| `halt` | Immediately halt the CPU (`cli; hlt`) |

---

## NodeV Scripting Language

NodeV is a complete interpreted language running entirely inside the kernel — no external runtime, no OS syscalls. The interpreter is a hand-written recursive-descent parser and tree-walking evaluator (~973 lines, `kernel/nodev.cpp`).

### Your first script

```bash
write /hello.nod pout("Hello, NodOS!\n");
nodev /hello.nod
```

Or write multi-line scripts with `vi`:

```bash
vi /myscript.nod
```

### Types

```javascript
int    x = 42;
float  f = 3.14;
string s = "hello";
list   nums;          // dynamic array
```

### Output and input

```javascript
pout("x = ", x, "\n");    // print any number of args — no implicit newline
pin(name);                 // read a line from keyboard → stores in 'name'
```

### Operators

```javascript
// Arithmetic
int r = (a + b) * c / 2 % 7;

// Bitwise
int b = x & 0xFF | y ^ z;
int s = val << 2;

// Comparison  (return 1 or 0)
int ok = (x == y);
int gt = (a > b);

// Logical
int both   = (x > 0 && y > 0);
int either = (x || y);
int inv    = !x;
```

### Control flow

```javascript
if (x > 10) {
    pout("big\n");
} else {
    pout("small\n");
}

int i = 0;
while (i < 5) {
    pout(i, " ");
    i = i + 1;
}

// for uses commas (not semicolons)
for (int j = 0, j < 10, j = j + 1) {
    pout(j, "\n");
}
```

### Functions

Functions are **hoisted** — callable before their definition in the file.

```javascript
function factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

pout(factorial(7), "\n");   // 5040
```

### Lists

```javascript
list scores;
scores[0] = 95;
scores[1] = 87;
pout("Best: ", scores[0], "\n");
```

### Strings

```javascript
string a = "Nod";
string b = a + "OS";        // concatenation
pout(b[0], "\n");            // character index → 'N'
pout(b == "NodOS", "\n");    // comparison → 1
```

### Complete example — FizzBuzz

```javascript
for (int i = 1, i <= 20, i = i + 1) {
    if (i % 15 == 0) {
        pout("FizzBuzz\n");
    } else {
        if (i % 3 == 0) {
            pout("Fizz\n");
        } else {
            if (i % 5 == 0) {
                pout("Buzz\n");
            } else {
                pout(i, "\n");
            }
        }
    }
}
```

### Complete example — interactive calculator

```javascript
pout("Enter a: "); pin(a);
pout("Enter b: "); pin(b);
int x = a + 0;
int y = b + 0;
pout("Sum:  ", x + y, "\n");
pout("Diff: ", x - y, "\n");
pout("Prod: ", x * y, "\n");
```

### NodeV kernel limits

| Limit | Value |
|-------|-------|
| Memory arena | 128 KB (reset per script run) |
| Max global functions | 16 |
| Max lists | 8 |
| Max list items | 64 per list |
| Max function parameters | 6 |
| Max call arguments | 8 |
| Iteration guard | 200,000 iterations |
| Name / string length | 63 chars |

---

## vi Text Editor

Open any file with:

```
vi <filename>
```

Creates the file if it doesn't exist. The status bar always shows: **mode · filename · `[+]` if unsaved · row,col**.

### Modes

| Mode | Colour | How to enter |
|------|--------|-------------|
| **Normal** | Grey bar | Default on open, or `Esc` |
| **Insert** | Blue bar | `i` `a` `A` `I` `o` `O` |
| **Command** | Yellow bar | `:` in Normal mode |

### Normal mode keys

| Key(s) | Action |
|--------|--------|
| `h` `j` `k` `l` / arrows | Move left / down / up / right |
| `0` | Jump to start of line |
| `$` | Jump to end of line |
| `gg` | Jump to first line |
| `G` | Jump to last line |
| `i` | Insert before cursor |
| `a` | Insert after cursor |
| `A` | Insert at end of line |
| `I` | Insert at start of line |
| `o` | Open new line below, enter Insert |
| `O` | Open new line above, enter Insert |
| `x` | Delete character under cursor |
| `dd` | Delete entire current line |
| `:` | Enter Command mode |

### Insert mode

Type normally. `Backspace` deletes (joins lines at column 0). `Enter` splits the line. Arrow keys move the cursor. `Esc` returns to Normal.

### Command mode

| Command | Action |
|---------|--------|
| `:w` | Save to current filename |
| `:w <n>` | Save as a different filename |
| `:q` | Quit (blocked if unsaved changes exist) |
| `:q!` | Force quit without saving |
| `:wq` / `:x` | Save and quit |
| `:<n>` | Jump to line number *n* |

---

## Power Management

Three shutdown levels, implemented in `kernel/power.cpp`:

### `shutdown`

Attempts to power off by writing to well-known ACPI/emulator control ports in sequence:

| Port | Value | Target |
|------|-------|--------|
| `0x604` | `0x2000` | QEMU PIIX4 ACPI (default) |
| `0x4004` | `0x3400` | VirtualBox ACPI |
| `0xB004` | `0x2000` | Bochs / older QEMU |

If none succeed (real hardware, or unsupported emulator), it halts with `cli; hlt` and prints *"It is now safe to turn off your computer."*

> **QEMU tip:** The default ACPI port `0x604` works on modern QEMU with no extra flags. If it doesn't respond, add `-device isa-debug-exit,iobase=0x604,iosize=0x4` to your QEMU command.

### `reboot`

Performs a hard reset via the **8042 keyboard controller**:
1. Spins until input buffer is empty (port `0x64` bit 1 = 0)
2. Writes reset pulse `0xFE` to port `0x64`

Works on all x86 hardware and emulators.

### `halt`

Executes `cli; hlt` in a loop. All interrupts disabled, CPU stops permanently. Safe freeze without powering off.

---

## Terminal Pager

The built-in pager (`kernel/pager.cpp`) automatically pauses long output so it doesn't scroll past the top of the 25-row screen.

```
line 1
line 2
...
line 22
--More--        ← press any key to continue
```

- Pauses every **22 lines** (leaving room for the prompt and the bar)
- Press **any key** to show the next page
- The `--More--` text clears itself after each keypress
- The pager disables itself while printing `--More--` to prevent recursion

### Using the pager API in your own commands

```cpp
pager_enable();
for (int i = 0; i < 100; i++) {
    vga.println(items[i]);
    pager_check();       // pauses every 22 lines
}
pager_disable();
```

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  boot/boot.asm      Multiboot header, 16 KB BSS stack        │
│  boot/cpu.asm       GDT flush, IDT load, ISR/IRQ stubs       │
├──────────────────────────────────────────────────────────────┤
│  kernel/kernel.cpp   kernel_main() — full init sequence      │
│                                                              │
│  ┌────────────┐  ┌────────┐  ┌──────────┐  ┌────────────┐    │
│  │ gdt / idt  │  │  pmm   │  │   heap   │  │ pit / kbd  │    │
│  │ 5 GDT segs │  │ 256 MB │  │  8 MB    │  │ 100 Hz     │    │
│  │ 256 IDT    │  │ bitmap │  │ kmalloc  │  │ IRQ 0/1    │    │
│  └────────────┘  └────────┘  └──────────┘  └────────────┘    │
│                                                              │
│  ┌──────────────────┐  ┌──────────────────────────────────┐  │
│  │  process         │  │  VFS                             │  │
│  │  round-robin     │  │  64-entry MFT in RAM             │  │
│  │  16 tasks        │  │  ATA PIO sector I/O              │  │
│  │  8 KB stacks     │  │  4M-sector free-space bitmap     │  │
│  └──────────────────┘  └──────────────────────────────────┘  │
│                                                              │
│  ┌──────────┐  ┌──────────────────────────┐  ┌──────────┐    │
│  │  shell   │  │  NodeV interpreter       │  │  vi      │    │
│  │  23 cmds │  │  lexer→parser→tree-walk  │  │  3 modes │    │
│  │  pager   │  │  128 KB arena            │  │  :w :q   │    │
│  └──────────┘  └──────────────────────────┘  └──────────┘    │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  power.cpp   shutdown (ACPI) / reboot (8042) / halt  │    │
│  └──────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
```

### Key design decisions

**No global C++ constructors** — `-ffreestanding` means `.init_array` is never called. Every subsystem has an explicit `foo_init()` called in strict order from `kernel_main()`.

**No STL, exceptions, or RTTI** — flags: `-fno-exceptions -fno-rtti -nostdlib`. The `kstring` module provides all string and memory primitives including `k_strtok`, `k_strstr`, `k_strchr`.

**ATA PIO polling** — no DMA, no disk interrupts. Simple and reliable in QEMU. Cache flush (`0xE7`) issued after every write to guarantee the `.img` file is updated.

**MFT persistence** — the entire 64-entry directory table is flushed to disk after every mutation via `save_mft()`. The sector bitmap lives only in RAM and is fully reconstructed on boot by scanning the MFT.

**Scheduler via `irq_common`** — `irq_handler(uint32_t esp)` in `idt.cpp` calls `scheduler_tick(esp)` on every timer IRQ. The return value is the new stack pointer, enabling a full context switch with zero extra assembly beyond what `irq_common` already does.

---

## Project Structure

```
nodos/
├── boot/
│   ├── boot.asm          Multiboot header, 16 KB BSS stack, _start → kernel_main
│   └── cpu.asm           gdt_flush, idt_load, ISR macros (0-31), IRQ macros (0-15)
│
├── include/
│   ├── vga.h             80×25 VGA text driver (write_cell, set_cursor, VGAColor)
│   ├── gdt.h             GDTEntry, GDTPtr structs
│   ├── idt.h             IDTEntry, IDTPtr, Registers, IRQHandler, ISRHandler
│   ├── io.h              inb / outb / inw / outw / io_wait  (inline asm)
│   ├── pmm.h             PMM API, PMM_PAGE_SIZE, PMM_RAM_SIZE (256 MB), pmm_get_total_ram_mb
│   ├── heap.h            kmalloc / kfree / krealloc
│   ├── pit.h             pit_init, pit_ticks, pit_uptime_ms/s, pit_sleep
│   ├── keyboard.h        keyboard_init, keyboard_getchar, keyboard_readline, keyboard_available
│   ├── ata.h             ata_init, ata_read_sector, ata_write_sector
│   ├── vfs.h             VFSEntry, VFS_MAX_*, all file/dir operations, vfs_get_disk_size_mb
│   ├── process.h         Process struct, MAX_PROCESSES (16), PROCESS_STACK (8 KB)
│   ├── kstring.h         Full string library including k_strtok, k_strchr, k_strstr
│   ├── nodev.h           nodev_exec, nodev_run_file
│   ├── vi.h              vi_open
│   ├── power.h           system_shutdown, system_reboot
│   ├── pager.h           pager_enable, pager_disable, pager_check, pager_enabled
│   └── shell.h           shell_init, shell_run, shell_exec, cmd_fetch
│
├── kernel/
│   ├── kernel.cpp        kernel_main, gradient boot banner with live RAM/disk stats
│   ├── gdt.cpp           5-descriptor GDT (null, k-code, k-data, u-code, u-data)
│   ├── idt.cpp           IDT setup, PIC remap (IRQ→INT 32-47), isr_handler, irq_handler
│   ├── pmm.cpp           Bitmap PMM, pmm_get_total_ram_mb
│   ├── heap.cpp          First-fit allocator with split + coalesce, krealloc
│   ├── pit.cpp           PIT channel 0, 100 Hz, uptime helpers, pit_sleep
│   ├── keyboard.cpp      PS/2 scancode decode, shift/caps-lock, 256-byte ring buffer
│   ├── kstring.cpp       k_strlen, k_strcmp, k_strcpy, k_strcat, k_strtok, k_strstr, …
│   ├── ata.cpp           ATA PIO primary master, read/write with BSY+DRQ polling
│   ├── vfs.cpp           MFT filesystem: 4M-sector bitmap, save_mft, all VFS ops
│   ├── process.cpp       Process table, stack frame setup, round-robin scheduler_tick
│   ├── power.cpp         shutdown (3 ACPI ports), reboot (8042 pulse), halt
│   ├── pager.cpp         22-line --More-- pager with auto disable/re-enable
│   ├── nodev.cpp         NodeV: lexer + recursive-descent parser + tree-walk eval (973 lines)
│   ├── vi.cpp            vi: Normal/Insert/Command modes, full cursor movement (618 lines)
│   └── shell.cpp         23 commands, tokeniser, coloured prompt, cmd_fetch (407 lines)
│
├── config/
│   └── grub.cfg          GRUB 2 menu entry
│
├── linker.ld             Kernel link script (load at 1 MB / 0x100000)
├── Makefile              Build, disk creation, QEMU launch
└── README.md             This file
```

---

## Boot Sequence

```
BIOS / UEFI
  └─ GRUB 2  (reads nodos.iso)
       └─ loads nodos.bin at 0x100000 (1 MB)
            └─ boot.asm  _start:
                    mov esp, stack_top        ← 16 KB BSS stack
                    push ebx                  ← multiboot info pointer
                    push eax                  ← multiboot magic (0x2BADB002)
                    call kernel_main
                         │
                         ▼
               kernel_main():
                 1.  vga.init()              clear screen, reset colour
                 2.  print_banner()          gradient ASCII art + live RAM/disk stats
                 3.  gdt_init()              load 5-entry GDT, far-jump to reload CS
                 4.  idt_init()              256 IDT gates, remap PIC (IRQ→INT 32-47)
                 5.  pmm_init()              zero bitmap, mark first 1MB + kernel used
                 6.  heap_init()             8 MB contiguous heap above kernel
                 7.  pit_init(100)           PIT channel 0 → 100 Hz, install IRQ 0
                 8.  keyboard_init()         PS/2 IRQ 1 handler
                 9.  sti                     enable hardware interrupts
                10.  process_init()          PID 1 = shell, zero procs[] table
                11.  ata_init()              print ATA status to VGA
                12.  vfs_init()              read MFT from LBA 1-127, rebuild bitmap
                13.  shell_init()            seed /home dir, /readme.txt, /hello.nod
                14.  shell_run()             ← never returns
```

---

## Interrupt Handling

`boot/cpu.asm` defines two macro families that generate all 48 stubs:

### ISR stubs — exceptions 0–31

```nasm
ISR_NO_ERR n    ; pushes dummy 0 + int_no, jumps isr_common
ISR_ERR    n    ; CPU already pushed real err_code; just pushes int_no
```

`isr_common` saves the full register state (`pusha` + DS), switches to kernel data segment, then calls `isr_handler(Registers*)` in C++. On return, segment registers and all GPRs are restored, then `iret` resumes execution.

### IRQ stubs — IRQ 0–15 → INT 32–47

```nasm
IRQ n, int_no   ; pushes dummy err + int_no, jumps irq_common
```

`irq_common` saves state identically to `isr_common`, then calls `irq_handler(uint32_t esp)` — passing the current stack pointer as the argument. The return value (`eax`) is the ESP to restore, which is then loaded with `mov esp, eax`. This single instruction is how the scheduler performs a **full context switch** — `scheduler_tick` can return any process's saved ESP and the CPU will resume that process transparently on `iret`.

---

## Dependencies

### Build tools

| Package | Purpose |
|---------|---------|
| `nasm` | Assemble `boot.asm` and `cpu.asm` |
| `g++-multilib` | 32-bit C++ compilation (`-m32`) |
| `make` | Build system |

### ISO creation

| Package | Purpose |
|---------|---------|
| `grub-pc-bin` | `grub-mkrescue` tool |
| `grub-common` | GRUB 2 modules |
| `xorriso` | ISO 9660 image writer |
| `mtools` | FAT image support (required by grub-mkrescue) |

### Emulator

| Package | Purpose |
|---------|---------|
| `qemu-system-x86` | Run the ISO |
| `libgtk-3-0` | QEMU GTK display window |

### Install all at once

```bash
# Ubuntu / Debian / Kali
sudo apt update && sudo apt install \
  nasm g++-multilib make \
  grub-pc-bin grub-common xorriso mtools \
  qemu-system-x86

# Arch Linux
sudo pacman -S nasm gcc make grub libisoburn mtools qemu-arch-extra

# Fedora / RHEL
sudo dnf install nasm gcc-c++ make grub2-tools xorriso mtools qemu-system-x86
```

---

## Building

```bash
make              # compile everything → nodos.iso
make run          # build + launch QEMU with disk attached
make clean        # remove build/, isodir/, nodos.iso  (disk image is kept)
```

### Makefile key variables

| Variable | Default | Description |
|----------|---------|-------------|
| `DISK_SIZE` | `2048` | Disk size in MB (set to `4096` for 4 GB) |
| `DISK_IMG` | `nodos_disk.img` | Raw disk image filename |

### QEMU flags used

```
-cdrom nodos.iso
-boot d
-m 256M
-drive file=nodos_disk.img,format=raw,if=ide,bus=0,unit=0,cache=writethrough
-serial mon:stdio
-display gtk
-no-reboot -no-shutdown
```

### Snap QEMU fix (Kali / Ubuntu)

If you see `libpthread: undefined symbol __libc_pthread_init`, QEMU is a snap package. The Makefile uses `env -i` to strip snap's broken `LD_PRELOAD`. If that still fails:

```bash
sudo apt remove qemu-system-x86
sudo apt install --no-install-recommends qemu-system-x86
```

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Black screen after GRUB | Global C++ constructor running before `vga.init()` | Never add global objects with constructors; use explicit `init()` |
| Files lost after reboot | ATA write failing silently | Verify `ata_init()` message at boot; check disk image exists |
| `shutdown` doesn't power off | Emulator doesn't recognise ACPI port | Add `-device isa-debug-exit,iobase=0x604,iosize=0x4` to QEMU |
| NodeV script hangs | Hit 200,000 iteration guard | Check for infinite loops |
| NodeV `for` loop errors | Using semicolons instead of commas | Syntax is `for (int i=0, i<n, i=i+1)` |
| vi won't quit | Unsaved changes | Use `:q!` to force quit |
| `--More--` appears unexpectedly | Pager left enabled by a command | Pager auto-disables; this shouldn't happen with shipped commands |
| Kernel panic on boot | Stack overflow or null pointer | Check that new drivers don't use large stack arrays |
| `make disk` is slow | `dd` writing 2 GB of zeros | Normal — takes ~5-15 s. Use `status=progress` to monitor |

---

## Extending NodOS

### Adding a shell command

1. Write a `static void cmd_foo(const char* arg)` function in `kernel/shell.cpp`
2. Add help text in `cmd_help()`: `vga.println("  foo <arg>   what it does");`
3. Add a dispatcher line in `shell_exec()`: `if (k_strcmp(a0,"foo")==0) { cmd_foo(a1); return; }`

### Adding a kernel driver

Golden rule: **never use global C++ constructors**.

```cpp
// include/mydrv.h
#pragma once
void mydrv_init();
void mydrv_do(int x);

// kernel/mydrv.cpp
#include "../include/mydrv.h"
static int state = 0;         // primitive globals are fine (zero-init by linker)
void mydrv_init() { state = 1; }
void mydrv_do(int x) { state += x; }
```

Add `mydrv_init()` call in `kernel_main()` at the right point, and add `$(BUILD_DIR)/mydrv.o` to `OBJS` in the Makefile.

### Adding a NodeV built-in function

In `kernel/nodev.cpp`, inside `nv_eval`, before the user function table lookup:

```cpp
if (k_strcmp(e->sval, "abs") == 0 && e->argc == 1) {
    NVValue a = nv_eval(e->args[0], sc);
    return nv_int(a.i < 0 ? -a.i : a.i);
}
```

### Using the pager in a new command

```cpp
static void cmd_longlist() {
    pager_enable();
    for (int i = 0; i < 100; i++) {
        vga.println(items[i]);
        pager_check();       // pauses every 22 lines
    }
    pager_disable();
}
```

### Boot order cheat sheet

```
vga.init()        ALWAYS first — needed by all ok()/warn() messages
gdt_init()        before IDT
idt_init()        before any IRQ handler registration
pmm_init()        before heap
heap_init()       before kmalloc users
pit_init()        registers IRQ 0  (IDT must be ready)
keyboard_init()   registers IRQ 1  (IDT must be ready)
sti               AFTER all handlers are registered
process_init()    needs heap (kmalloc for process stacks)
ata_init()        needs interrupts enabled
vfs_init()        needs ATA; reads MFT from disk
shell_init()      needs VFS
shell_run()       never returns
```

---
