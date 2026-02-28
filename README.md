# NodOS

A hobby x86 32-bit operating system built from scratch in C++ and NASM assembly.  
Boots via GRUB 2 (Multiboot), runs in protected mode, and drops into an interactive shell.

```
  _   _           _  ___  ____
 | \ | | ___   __| |/ _ \/ ___|
 |  \| |/ _ \ / _` | | | \___ \
 | |\  | (_) | (_| | |_| |___) |
 |_| \_|\___/ \__,_|\___/|____/

  NodOS v1.0  |  x86 32-bit Hobby OS
```

---

## Table of Contents

- [Features](#features)
- [Project Structure](#project-structure)
- [Architecture Overview](#architecture-overview)
- [Building](#building)
- [Running](#running)
- [Shell Commands](#shell-commands)
- [Phase Breakdown](#phase-breakdown)
- [How It Works](#how-it-works)
- [Extending NodOS](#extending-nodos)

---

## Features

- Multiboot-compliant kernel bootable with GRUB 2
- 32-bit x86 protected mode
- GDT with kernel and user segment descriptors
- IDT with full exception handling and hardware IRQ dispatch
- PIC remapping (IRQs 0–15 → INTs 32–47)
- Bitmap-based physical memory manager (PMM)
- Linked-list heap allocator (`kmalloc` / `kfree`)
- PIT timer at 100 Hz with `pit_sleep()` support
- PS/2 keyboard driver with scancode-to-ASCII translation and ring buffer
- In-memory virtual filesystem (VFS) — 32 files × 4 KB
- Round-robin preemptive scheduler via timer IRQ stack swapping
- Interactive shell with 14 built-in commands
- VGA 80×25 colour text driver with scrolling and backspace

---

## Project Structure

```
NodOS/
├── boot/
│   ├── boot.asm          # Multiboot header, stack setup, _start entry point
│   └── cpu.asm           # GDT flush, IDT load, ISR/IRQ stubs (scheduler-aware)
├── config/
│   └── grub.cfg          # GRUB 2 menu entry
├── include/              # All header files
│   ├── io.h              # inb / outb / inw / outw port I/O helpers
│   ├── vga.h             # VGADriver class (colour text, scroll, printInt, printHex)
│   ├── kstring.h         # Freestanding string library (strlen, strcmp, itoa, …)
│   ├── gdt.h             # GDT structures and gdt_init()
│   ├── idt.h             # IDT structures, Registers struct, handler typedefs
│   ├── pmm.h             # Physical memory manager
│   ├── heap.h            # kmalloc / kfree / krealloc
│   ├── pit.h             # PIT timer driver
│   ├── keyboard.h        # PS/2 keyboard driver
│   ├── vfs.h             # In-memory virtual filesystem
│   ├── process.h         # Process struct, scheduler API
│   └── shell.h           # Shell public API
├── kernel/               # All C++ implementation files
│   ├── kernel.cpp        # kernel_main() — boot sequence entry point
│   ├── kstring.cpp       # String utilities (no libc dependency)
│   ├── gdt.cpp           # GDT initialisation (5 descriptors)
│   ├── idt.cpp           # IDT setup, PIC remap, exception/IRQ dispatch
│   ├── pmm.cpp           # Bitmap page allocator (32 MB, 4 KB pages)
│   ├── heap.cpp          # Linked-list heap
│   ├── pit.cpp           # 8253 PIT driver at 100 Hz
│   ├── keyboard.cpp      # PS/2 keyboard IRQ handler + readline
│   ├── vfs.cpp           # In-memory filesystem operations
│   ├── process.cpp       # Round-robin scheduler + context switch
│   └── shell.cpp         # Interactive shell + all 14 commands
├── linker.ld             # Linker script — kernel loaded at 1 MB, exports kernel_end
└── Makefile
```

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        GRUB 2                               │
│              loads kernel at physical 0x100000              │
└────────────────────────┬────────────────────────────────────┘
                         │  Multiboot handoff
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  boot.asm (_start)                                          │
│  • Sets up 16 KB stack                                      │
│  • Calls kernel_main(magic, mb_info)                        │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  kernel_main()  — boot sequence                             │
│                                                             │
│  Phase 1  vga.init()        ← VGA driver, clear screen     │
│  Phase 2  gdt_init()        ← load 5-descriptor GDT        │
│           idt_init()        ← IDT + PIC remap to 0x20/0x28 │
│           pmm_init()        ← bitmap page allocator        │
│           heap_init()       ← 2 MB kmalloc arena           │
│  Phase 3  pit_init(100)     ← 100 Hz timer on IRQ 0        │
│           keyboard_init()   ← PS/2 keyboard on IRQ 1       │
│           sti               ← enable hardware interrupts   │
│  Phase 5  process_init()    ← scheduler, PID 1 = shell     │
│  Phase 4  vfs_init()        ← in-memory filesystem         │
│           shell_init()      ← pre-populate readme.txt      │
│                                                             │
│  → shell_run()              ← interactive shell loop       │
└─────────────────────────────────────────────────────────────┘
```

---

## Building

### Prerequisites

| Tool | Purpose |
|------|---------|
| `nasm` | Assembles `boot.asm` and `cpu.asm` |
| `g++` with `-m32` support | Compiles C++ kernel code for 32-bit x86 |
| `gcc` with `-m32` support | Links the final ELF binary |
| `grub-mkrescue` | Creates the bootable ISO |
| `xorriso` | Required by grub-mkrescue |
| `qemu-system-i386` | Runs the OS in a VM |

On Ubuntu / Debian:

```bash
sudo apt install nasm gcc g++ grub-pc-bin grub-common xorriso qemu-system-x86
```

If your host is 64-bit you also need the 32-bit cross-compile libraries:

```bash
sudo apt install gcc-multilib g++-multilib
```

### Build

```bash
make          # compiles everything and produces nodos.iso
make clean    # remove all build artefacts
```

---

## Running

```bash
make run
```

This launches QEMU with:

| Flag | Effect |
|------|--------|
| `-cdrom nodos.iso` | Boot from the ISO |
| `-m 512M` | 512 MB RAM |
| `-serial mon:stdio` | Serial output + QEMU monitor on the same terminal |
| `-display gtk,grab-on-hover=off` | GTK window — does **not** capture your mouse |
| `-no-reboot` | Halts instead of rebooting on crash |
| `-no-shutdown` | Keeps the QEMU window open on shutdown |

### QEMU keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+A` then `X` | Quit QEMU immediately |
| `Ctrl+A` then `C` | Open QEMU monitor (type `quit` to exit) |
| `Ctrl+Alt+G` | Release mouse/keyboard from the QEMU window |

---

## Shell Commands

Once booted you will see the NodOS shell prompt:

```
nodos@kernel>
```

| Command | Description |
|---------|-------------|
| `help` | List all available commands |
| `clear` | Clear the screen |
| `info` | Show OS and hardware information |
| `mem` | Physical and heap memory usage |
| `ls` | List all files in the VFS |
| `cat <file>` | Print the contents of a file |
| `write <file> <text>` | Write text to a file (creates if needed) |
| `rm <file>` | Delete a file |
| `echo <text>` | Print text to the screen |
| `time` | Show uptime (hh:mm:ss) and raw tick count |
| `ps` | List all running processes with PID and state |
| `kill <pid>` | Kill a process by its PID |
| `reboot` | Reboot the system via PS/2 controller reset |
| `halt` | Disable interrupts and halt the CPU |

### Example session

```
nodos@kernel> info
=== System Information ===
  OS:         NodOS v1.0
  Arch:       x86 (32-bit protected mode)
  Bootloader: GRUB 2 (Multiboot)
  VGA:        80x25 colour text mode
  RAM:        32 MB (PMM tracked)
  Uptime:     3 seconds

nodos@kernel> write hello.txt Hello, NodOS World!
Wrote 19 bytes to hello.txt

nodos@kernel> ls
  hello.txt   (19 bytes)
  readme.txt  (104 bytes)

nodos@kernel> cat hello.txt
Hello, NodOS World!

nodos@kernel> mem
=== Memory ===
  Physical  used : 768 KB / 32768 KB
  Physical  free : 32000 KB
  Heap      used : 0 KB / 2048 KB

nodos@kernel> ps
  PID  STATE    NAME
  1    RUNNING  shell

nodos@kernel> time
=== Uptime ===
  00:00:07
  Ticks: 703 @ 100 Hz

nodos@kernel> rm hello.txt
Deleted: hello.txt
```

---

## Phase Breakdown

### Phase 1 — Foundation
- `boot.asm` — Multiboot header, 16 KB BSS stack, `_start` entry point
- `vga.cpp` (header-only) — 80×25 VGA text driver, colour, scrolling, backspace
- `kernel_main()` — C++ kernel entry point
- **Important:** `vga.init()` must be called explicitly — global C++ constructors are never invoked in a freestanding kernel environment

### Phase 2 — CPU & Memory
- **GDT** (`gdt.cpp`) — 5 descriptors: null, kernel code (ring 0), kernel data (ring 0), user code (ring 3), user data (ring 3)
- **IDT** (`idt.cpp`) — 256-entry table; exceptions 0–31 call `isr_handler`, hardware IRQs 0–15 (remapped to INTs 32–47) call `irq_handler`
- **PIC remap** — Master PIC offset → 0x20, Slave PIC offset → 0x28 (avoids conflicts with CPU exception vectors)
- **PMM** (`pmm.cpp`) — Bitmap allocator; one bit per 4 KB page across a 32 MB address space; marks first 1 MB + kernel image pages as used at boot
- **Heap** (`heap.cpp`) — Linked-list allocator placed just above the kernel image; `kmalloc`, `kfree`, `krealloc`; free blocks are coalesced on `kfree`

### Phase 3 — Hardware Drivers
- **PIT** (`pit.cpp`) — Programs the 8253 PIT channel 0 at 100 Hz; increments a tick counter on every IRQ 0; provides `pit_uptime_ms()`, `pit_uptime_s()`, and `pit_sleep(ms)`
- **PS/2 Keyboard** (`keyboard.cpp`) — IRQ 1 handler reads scancodes from port 0x60, translates to ASCII using scancode maps (with Shift and Caps Lock support), buffers characters in a 256-byte ring buffer; `keyboard_readline()` handles backspace and echoes to VGA

### Phase 4 — Shell & Filesystem
- **VFS** (`vfs.cpp`) — Flat array of 32 file slots, each holding up to 4 KB of data; supports create, read, write, append, delete, and list; all data is lost on reboot
- **Shell** (`shell.cpp`) — Reads lines via `keyboard_readline()`, tokenises with a simple `split_args()` function, and dispatches to one of 14 command handlers

### Phase 5 — Processes
- **Scheduler** (`process.cpp`) — Round-robin preemptive scheduling via the timer IRQ; `irq_handler` in `idt.cpp` calls `scheduler_tick(esp)` on every tick; the function saves the current task's ESP, picks the next READY process, and returns its ESP — `irq_common` in `cpu.asm` then moves `esp` to the returned value before `iret`, completing the context switch
- `process_create()` — Allocates a kernel stack with `kmalloc`, builds a fake IRQ stack frame so the new task starts executing at the given entry function after its first context switch
- `process_kill()` — Marks the process as DEAD; the scheduler cleans up memory on the next tick
- `process_sleep(ms)` — Sets the process to SLEEPING with a wake tick, then halts; the scheduler wakes it when `pit_ticks()` reaches the target

---

## How It Works

### Boot flow

1. BIOS loads GRUB from the boot device
2. GRUB reads `grub.cfg`, loads `nodos.bin` at physical address `0x100000` (1 MB)
3. GRUB verifies the Multiboot header in the `.multiboot` section
4. Control jumps to `_start` in `boot.asm`
5. `_start` sets `esp` to `stack_top` (16 KB stack in `.bss`), then calls `kernel_main`
6. `kernel_main` initialises each subsystem in order and hands off to `shell_run()`

### Context switching

The scheduler works entirely through the timer IRQ stack. When IRQ 0 fires:

1. The CPU pushes `eip`, `cs`, `eflags` onto the current stack
2. `irq_common` (cpu.asm) pushes all general-purpose registers and `ds` — forming a complete `Registers` frame
3. `irq_handler` (idt.cpp) is called with the current `esp` as an argument
4. `scheduler_tick(esp)` saves that ESP into the current process struct, picks the next process, and returns the new process's saved ESP
5. `irq_handler` returns the new ESP to `irq_common`
6. `irq_common` sets `esp = eax` (the returned new ESP), pops all registers, and executes `iret` — resuming the new process exactly where it left off

### Why no global C++ constructors?

In a freestanding kernel, the C++ runtime startup code (`crti.o`, `crtbegin.o`, etc.) is not linked, so `.init_array` / `.ctors` sections are never walked. Any global object with a non-trivial constructor will have its constructor silently skipped. NodOS works around this by using an explicit `vga.init()` call and avoiding non-trivial global constructors throughout.

---

## Extending NodOS

Some ideas for taking NodOS further:

| Idea | Where to start |
|------|---------------|
| Persist files across reboots | Add an ATA/IDE driver and implement FAT32 on top of VFS |
| User-space programs | Add a `syscall` instruction handler (INT 0x80), map user stacks with page tables |
| Virtual memory per process | Implement a page directory per process; switch CR3 on context switch |
| Graphics mode | Switch to VGA mode 0x13 (320×200, 256 colours) via BIOS INT 0x10 before protected mode |
| Network stack | Write an RTL8139 NIC driver (PCI), implement ARP + UDP |
| Port a C library | musl libc can be cross-compiled for bare-metal i686 with minimal changes |
| Serial console | Port 0x3F8 (COM1) — useful for debug output inside QEMU (`-serial stdio`) |
| SMP | Start Application Processors via the APIC INIT/SIPI sequence |

---

## To Change persistent storage

### 1. Update Makefile
Find the `disk.img` target (usually around line 35) and change the `2G` flag to your desired size (`1G` or `4G`).

```makefile
# Change the size at the end of this line (e.g., to 4G)
disk.img:
	qemu-img create -f raw disk.img 4G
```

### 2. Update kernel/vfs.cpp
Adjust the `MAX_SECTORS` macro near the top of the file so your RAM bitmap knows exactly how many sectors exist.

* **For 1 GB:** Use `2097152`
* **For 4 GB:** Use `8388608`

```cpp
// Update this line near the top of vfs.cpp (Example for 4GB)
#define MAX_SECTORS 8388608 
static uint32_t disk_bitmap[MAX_SECTORS / 32];
```

### 3. Delete the Old Drive & Rebuild
Because `make` will not overwrite an existing file, you must delete your old 2 GB drive so the system generates a fresh one with the new capacity. Run this in your Linux terminal:

```bash
rm disk.img
make run
```