# NodOS v4.0 — Graphical Edition

A hobby x86 32-bit operating system built entirely from scratch in C++ and NASM assembly.  
No STL. No libc. No RTTI. No exceptions. Just bare metal.

```
     _   _           _  ___  ____
    | \ | | ___   __| |/ _ \/ ___|
    |  \| |/ _ \ / _` | | | \___ \
    | |\  | (_) | (_| | |_| |___) |
    |_| \_|\___/ \__,_|\___/|____/

    NodOS v4.0 GUI  //  x86 32-bit  //  1024x768x32  //  256 MB RAM
```

---

## Table of Contents

1. [Features](#features)
2. [Architecture Overview](#architecture-overview)
3. [Repository Structure](#repository-structure)
4. [Build System](#build-system)
5. [Boot Flow](#boot-flow)
6. [File-by-File Reference](#file-by-file-reference)
   - [Assembly & Boot](#assembly--boot)
   - [Hardware Drivers](#hardware-drivers)
   - [Memory Management](#memory-management)
   - [Kernel Core](#kernel-core)
   - [Graphics & Framebuffer](#graphics--framebuffer)
   - [GUI Layer](#gui-layer)
   - [Window Manager](#window-manager)
   - [Applications](#applications)
   - [Filesystem & Shell](#filesystem--shell)
   - [Scripting Engine](#scripting-engine)
   - [Settings](#settings)
7. [What Each File Controls (Quick-Change Guide)](#what-each-file-controls-quick-change-guide)
8. [How to Add New Things](#how-to-add-new-things)
   - [Adding a Shell Command](#adding-a-shell-command)
   - [Adding a Desktop Icon](#adding-a-desktop-icon)
   - [Adding a Start Menu Item](#adding-a-start-menu-item)
   - [Adding a New GUI Window / App](#adding-a-new-gui-window--app)
   - [Adding a New System Call / Kernel Feature](#adding-a-new-system-call--kernel-feature)
   - [Adding a New IRQ Handler](#adding-a-new-irq-handler)
   - [Adding a New NodeV Built-in Function](#adding-a-new-nodev-built-in-function)
   - [Adding a New Settings Option](#adding-a-new-settings-option)
   - [Adding a New VFS File at Boot](#adding-a-new-vfs-file-at-boot)
   - [Adding a New Process / Background Task](#adding-a-new-process--background-task)
9. [Customization Reference](#customization-reference)
   - [Colors & Theme](#colors--theme)
   - [Screen Resolution](#screen-resolution)
   - [Boot Splash](#boot-splash)
   - [Terminal Behavior](#terminal-behavior)
   - [Keyboard Layout](#keyboard-layout)
   - [Wallpaper Gradient](#wallpaper-gradient)
   - [Hostname & Prompt](#hostname--prompt)
10. [NodeV Scripting Language](#nodev-scripting-language)
11. [Known Limitations](#known-limitations)
12. [Toolchain & Dependencies](#toolchain--dependencies)

---

## Features

- **Graphical desktop** at 1024×768×32bpp via GRUB-provided linear framebuffer
- **Double-buffered rendering** — full back-buffer, blit on `fb_swap()`
- **Window manager** — drag, focus, minimize, maximize, Z-order, close
- **Built-in applications**: Terminal, File Manager, Text Editor (vi), Settings
- **PS/2 keyboard + mouse** with software cursor (save/restore background pixels)
- **ATA PIO disk driver** + flat-table VFS (persistent across reboots)
- **NodeV scripting engine** — custom language with OOP, loops, functions, `$import`
- **Round-robin process scheduler** — preemptive, timer-driven (IRQ0)
- **Kernel heap** — linked-list allocator, 16 MB arena
- **Physical memory manager** — bitmap, 4 KB pages, 256 MB address space
- **VGA text fallback** — `nogui` boot flag drops to fullscreen terminal
- **Boot splash** — pixel-art logo, animated progress bar, iris-wipe transition
- **Runtime settings** — saved to `/settings.cfg`, loaded on every boot

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                     GUI Event Loop                      │
│         (gui.cpp — 60 fps, hlt-based frame cap)        │
├──────────────┬──────────────────┬───────────────────────┤
│  Desktop &   │  Window Manager  │  Applications         │
│  Taskbar     │  (wm.cpp)        │  Terminal / FileMgr   │
│  (desktop.cpp│                  │  Vi / Settings        │
├──────────────┴──────────────────┴───────────────────────┤
│              Framebuffer (fb.cpp — 1024×768×32)        │
├─────────────────────────────────────────────────────────┤
│   Shell (shell.cpp)    │   NodeV (nodev.cpp)            │
├────────────────────────┴────────────────────────────────┤
│          VFS (vfs.cpp) + ATA PIO (ata.cpp)             │
├─────────────────────────────────────────────────────────┤
│  PMM  │  Heap  │  PIT  │  IDT  │  GDT  │  Process      │
├───────┴────────┴───────┴───────┴───────┴───────────────┤
│              NASM boot.asm + cpu.asm                    │
│              GRUB Multiboot v1 loader                   │
└─────────────────────────────────────────────────────────┘
```

---

## Repository Structure

```
nodos/
├── include/                  # All header files
│   ├── ata.h                 # ATA PIO driver
│   ├── desktop.h             # Desktop, icons, taskbar, start menu
│   ├── fb.h                  # Framebuffer driver + color palette
│   ├── gdt.h                 # Global Descriptor Table
│   ├── gui.h                 # GUI init + main loop
│   ├── gui_filemanager.h     # File manager window
│   ├── gui_settings.h        # Settings window
│   ├── gui_terminal.h        # Graphical terminal window
│   ├── gui_vi.h              # GUI vi editor window
│   ├── heap.h                # Kernel heap allocator
│   ├── idt.h                 # Interrupt Descriptor Table
│   ├── io.h                  # x86 port I/O (inb/outb/inw/outw)
│   ├── keyboard.h            # PS/2 keyboard driver
│   ├── kstring.h             # Kernel string library
│   ├── mouse.h               # PS/2 mouse driver
│   ├── nodev.h               # NodeV scripting engine
│   ├── pager.h               # VGA output pager
│   ├── pit.h                 # Programmable Interval Timer
│   ├── pmm.h                 # Physical Memory Manager
│   ├── power.h               # Shutdown / reboot
│   ├── process.h             # Process scheduler
│   ├── settings_config.h     # Runtime settings struct
│   ├── shell.h               # Interactive shell
│   ├── splash.h              # Boot splash screen
│   ├── vfs.h                 # Virtual Filesystem
│   ├── vga.h                 # VGA text-mode driver
│   ├── vi.h                  # Terminal vi editor
│   └── wm.h                  # Window manager
│
├── kernel/                      # Implementation files
│   ├── ata.cpp
│   ├── desktop.cpp
│   ├── fb.cpp
│   ├── gdt.cpp
│   ├── gui.cpp
│   ├── gui_filemanager.cpp
│   ├── gui_settings.cpp
│   ├── gui_terminal.cpp
│   ├── gui_vi.cpp
│   ├── heap.cpp
│   ├── idt.cpp
│   ├── kernel.cpp            # kernel_main — entry point
│   ├── keyboard.cpp
│   ├── kstring.cpp
│   ├── mouse.cpp
│   ├── nodev.cpp
│   ├── pager.cpp
│   ├── pit.cpp
│   ├── pmm.cpp
│   ├── power.cpp
│   ├── process.cpp
│   ├── settings_config.cpp
│   ├── shell.cpp
│   ├── splash.cpp
│   ├── vfs.cpp
│   ├── vi.cpp
│   └── wm.cpp
│
├── asm/
│   ├── boot.asm              # Multiboot header + kernel entry
│   └── cpu.asm               # ISR/IRQ stubs, gdt_flush, idt_load
│
├── boot/
│   └── grub.cfg              # GRUB menu (GUI + Text entries)
│
├── linker.ld                 # Linker script (kernel at 0x100000)
├── Makefile                  # Build system
└── README.md                 # This file
```

---

## Build System

### Requirements

| Tool | Purpose |
|---|---|
| `i686-elf-g++` | Cross-compiler (freestanding C++, no STL) |
| `i686-elf-ld` | Cross-linker |
| `nasm` | Assembler for boot.asm / cpu.asm |
| `grub-mkrescue` | Creates bootable ISO |
| `xorriso` | Required by grub-mkrescue |
| `qemu-system-i386` | Testing in emulator |

### Building

```bash
# Build the kernel binary
make

# Build a bootable ISO
make iso

# Run in QEMU (GUI mode)
make run

# Run in QEMU (text/nogui mode)
make run-text

# Clean build artifacts
make clean
```

### Key Makefile Variables to Tweak

| Variable | Default | Effect |
|---|---|---|
| `CC` | `i686-elf-g++` | Change cross-compiler |
| `CFLAGS` | `-O2 -ffreestanding ...` | Optimization level, warnings |
| `QEMU_MEM` | `256M` | RAM given to QEMU |
| `QEMU_DISK` | path to disk image | ATA disk for VFS persistence |

---

## Boot Flow

```
GRUB loads nodos.bin (Multiboot v1)
    │
    ▼
boot.asm
  - Multiboot header (magic 0x1BADB002)
  - Sets up initial kernel stack
  - Calls kernel_main(magic, mb_info)
    │
    ▼
kernel_main()  [kernel.cpp]
  1. vga.init()                 — text-mode output for boot log
  2. Parse cmdline for "nogui"
  3. pmm_init(kernel_end)       — mark used pages
  4. heap_init(start, 16MB)     — kernel heap
  5. Print banner
  6. gdt_init()                 — load GDT
  7. idt_init()                 — load IDT, remap PIC
  8. pit_init(100)              — 100 Hz timer
  9. keyboard_init()            — IRQ1
 10. sti                        — enable interrupts
 11. process_init()             — PID 1 = shell
 12. ata_init() + vfs_init()    — disk + filesystem
 13. shell_init()
 14. settings_init()            — load /settings.cfg
 15. gui_init(mb_info)          — parse FB, init mouse, WM, desktop
     │
     ├─ nogui → gui_run_textmode()   (fullscreen terminal, never returns)
     └─ gui   → gui_run()           (desktop loop, never returns)
```

---

## File-by-File Reference

### Assembly & Boot

#### `boot.asm`
The very first code the CPU executes after GRUB hands off control.
- Declares the **Multiboot v1 header** (flags, magic `0x1BADB002`, checksum)
- Sets up a small initial **kernel stack** (typically 16 KB, `.bss` or explicit label)
- Calls `kernel_main(eax=magic, ebx=mb_info_addr)` in C++
- **Change here to**: adjust initial stack size, add early hardware resets, or switch to Multiboot v2

#### `cpu.asm`
Low-level interrupt infrastructure.
- **ISR stubs** `isr0`–`isr31` — CPU exception handlers; push dummy error code where CPU doesn't, then jump to `isr_common`
- **IRQ stubs** `irq0`–`irq15` — hardware interrupt wrappers, jump to `irq_common`
- `isr_common` — saves all registers (matching `Registers` struct), calls `isr_handler(Registers*)`
- `irq_common` — saves registers, calls `irq_handler(esp)`, restores returned ESP (enables task switching)
- `gdt_flush(ptr)` — executes `lgdt`, reloads segment registers
- `idt_load(ptr)` — executes `lidt`
- **Change here to**: add new exception stubs, change calling convention, add syscall gate

#### `grub.cfg`
GRUB boot menu.
- Entry 0: **GUI mode** — passes no extra args, boots full desktop
- Entry 1: **Text mode** — passes `nogui` on kernel cmdline, boots fullscreen terminal
- Both entries force `gfxpayload=1024x768x32` so GRUB never resets to VGA text before the kernel runs
- **Change here to**: add boot entries, change resolution (must also update `FB_WIDTH`/`FB_HEIGHT` in `fb.h`), change timeout, add kernel flags

#### `linker.ld`
Controls the memory layout of the kernel binary.
- Places `.text` (code) starting at physical address `0x100000` (1 MB) — standard for protected-mode kernels
- Defines `kernel_end` symbol — used by `pmm_init` and `heap_init` to know where kernel ends
- **Change here to**: move the kernel load address, add new sections (e.g. `.rodata`), adjust alignment

---

### Hardware Drivers

#### `ata.cpp` / `ata.h`
ATA PIO driver for the primary master disk.
- Uses ports `0x1F0`–`0x1F7` (primary bus)
- **LBA28** addressing — supports up to 128 GB disks
- `ata_read_sector(lba, buf)` — reads 256 words (512 bytes)
- `ata_write_sector(lba, buf)` — writes 256 words, then flushes cache (`0xE7`)
- **Change here to**: add DMA support, support slave/secondary bus, add error handling for bad sectors

#### `keyboard.cpp` / `keyboard.h`
PS/2 keyboard on IRQ1.
- Two scancode maps: `SCANCODE_MAP` (unshifted) and `SCANCODE_SHIFT` (shifted)
- Tracks `shift_held` and `caps_lock` state
- 256-byte ring buffer for keystrokes
- Arrow keys come through as: Up=`'A'`, Down=`'B'`, Right=`'C'`, Left=`'D'` (matching ANSI escape sequences)
- **Change here to**: add more key mappings, support international layouts, add function key handling, change buffer size

#### `mouse.cpp` / `mouse.h`
PS/2 mouse on IRQ12.
- Sends `0xFF` (reset), `0xF6` (defaults), `0xF4` (enable streaming) during init
- Reads 3-byte packets: flags byte, delta-X, delta-Y (Y is inverted)
- Click detection: **high-to-low transition** on left/right button
- Software cursor: 12×18 bitmap arrow, saves/restores background pixels each frame
- Position clamped to `[0, FB_WIDTH-1]` × `[0, FB_HEIGHT-1]`
- **Change here to**: change cursor shape (edit `CURSOR_MASK`), add scroll wheel support (4-byte packets), adjust sensitivity

#### `pit.cpp` / `pit.h`
8253/8254 Programmable Interval Timer.
- Initialized to **100 Hz** (`pit_init(100)`) — gives 10ms tick resolution
- `pit_uptime_ms()` = `ticks * 10`
- `pit_sleep(ms)` — busy-waits using `hlt` (yields until next IRQ)
- **Change here to**: change tick rate (affects scheduler granularity and `pit_uptime_ms` precision), add high-resolution timing

#### `power.cpp` / `power.h`
System shutdown and reboot.
- Shutdown tries three magic I/O ports in order: QEMU (`0x604`), VirtualBox/QEMU alt (`0x4004`), Bochs (`0xB004`)
- Falls back to `cli; hlt` loop if none work (real hardware)
- Reboot uses the **8042 PS/2 controller reset** (`outb(0x64, 0xFE)`) — works everywhere
- **Change here to**: add ACPI shutdown support, add a soft reboot via triple fault

---

### Memory Management

#### `pmm.cpp` / `pmm.h`
Physical Memory Manager.
- Bitmap allocator: 1 bit per 4 KB page, covering **256 MB** (`PMM_RAM_SIZE`)
- First 1 MB (BIOS/reserved) always marked used
- Pages from `0` up to `kernel_end` marked used at init
- `pmm_alloc()` — linear scan for first free bit; returns physical address
- `pmm_free(page)` — clears bit
- **Change here to**: increase `PMM_RAM_SIZE` for more RAM support, add multi-level bitmap for faster allocation, track page references

#### `heap.cpp` / `heap.h`
Kernel heap — `kmalloc` / `kfree` / `krealloc`.
- **Linked-list allocator** — each block has a `Block` header (size, free flag, next pointer)
- 8-byte alignment on all allocations
- Block splitting on alloc, **coalescing** (merge with next) on free
- Initialized with a **16 MB arena** starting at `kernel_end` (rounded to page boundary)
- **Change here to**: increase heap size in `kernel.cpp` (`heap_init` call), add allocation tracking/debugging, implement a faster slab allocator

---

### Kernel Core

#### `kernel.cpp`
The C++ kernel entry point — `kernel_main(magic, mb_info)`.
- Prints the boot banner (colorful VGA ASCII art)
- Orchestrates all subsystem initialization in order
- Detects `nogui` flag from GRUB cmdline
- Hands off to `gui_run()` or `gui_run_textmode()` (neither ever returns)
- **Change here to**: add new subsystem init calls, change heap size (`heap_init` second arg), change PIT frequency (`pit_init` arg), add multiboot v2 support

#### `gdt.cpp` / `gdt.h`
Global Descriptor Table — 5 entries.

| Index | Descriptor | Ring |
|---|---|---|
| 0 | Null | — |
| 1 | Kernel Code | 0 |
| 2 | Kernel Data | 0 |
| 3 | User Code | 3 |
| 4 | User Data | 3 |

- `gdt_flush` (in `cpu.asm`) loads the GDTR and reloads segment registers
- **Change here to**: add TSS entry (required for ring-3 tasks / syscalls), add per-CPU GDTs for SMP

#### `idt.cpp` / `idt.h`
Interrupt Descriptor Table.
- 32 exception gates (ISRs 0–31) + 16 hardware IRQ gates (mapped to INTs 32–47)
- PIC remapped so IRQ0 = INT32, IRQ1 = INT33, etc. (avoids conflict with CPU exceptions)
- `isr_handler` — dispatches to registered handler or triggers kernel panic
- `irq_handler` — dispatches to registered handler, sends EOI, then calls `scheduler_tick` on IRQ0 (timer)
- **Change here to**: add new exception handlers via `isr_install_handler`, add new device drivers via `irq_install_handler`

#### `process.cpp` / `process.h`
Cooperative + preemptive round-robin scheduler.
- Up to **16 processes** (`MAX_PROCESSES`)
- Each process gets an **8 KB kernel stack**
- Stack frame built by `setup_stack` to match what `irq_common` in `cpu.asm` expects (14 dwords)
- `scheduler_tick(esp)` — called from every timer IRQ; saves current ESP, wakes sleeping tasks, finds next READY task, returns its ESP
- `process_record_dummy(name)` — registers a display-only process (infinite sleep, no stack) for showing in `ps`
- **Change here to**: increase `MAX_PROCESSES`, increase `PROCESS_STACK`, add priority levels, add process-to-process messaging

#### `kstring.cpp` / `kstring.h`
Complete kernel string and memory library (no libc dependency).

| Function | Equivalent |
|---|---|
| `k_strlen` | `strlen` |
| `k_strcmp` / `k_strncmp` | `strcmp` / `strncmp` |
| `k_strcpy` / `k_strncpy` | `strcpy` / `strncpy` |
| `k_strcat` / `k_strncat` | `strcat` / `strncat` |
| `k_strchr` / `k_strstr` | `strchr` / `strstr` |
| `k_memset` / `k_memcpy` / `k_memmove` / `k_memcmp` | mem* family |
| `k_itoa` / `k_utoa` / `k_atoi` | number conversions |
| `k_strtok` | `strtok` |

**Change here to**: add `k_sprintf`, `k_sscanf`, Unicode support, or other string utilities needed by new subsystems.

#### `pager.cpp` / `pager.h`
VGA output pager (text mode only).
- Pauses every **22 lines** with `--More--` prompt and waits for a keypress
- Enable/disable globally with `pager_enable()` / `pager_disable()`
- **Change here to**: change the lines-per-page threshold, add `q` to abort output

---

### Graphics & Framebuffer

#### `fb.cpp` / `fb.h`
The entire 2D graphics engine.

**Initialization**: `fb_init(FBInfo)` — called with multiboot framebuffer info, allocates a heap back-buffer (`width × height × 4` bytes).

**Double buffering**: All drawing goes to `s_back`. Call `fb_swap()` to blit the full back-buffer to VRAM, or `fb_swap_rect(x,y,w,h)` for a dirty-rectangle optimization.

**Color palette** (`namespace Color`):

| Constant | Hex | Usage |
|---|---|---|
| `Desktop` | `0x1E3A5F` | Default desktop bg |
| `Taskbar` | `0x0F1F35` | Taskbar background |
| `WinTitle` | `0x2A5298` | Unfocused title bar |
| `WinTitleFoc` | `0x3D6FCC` | Focused title bar |
| `WinBody` | `0x0E1E30` | Window client area |
| `TermBg` | `0x0A0F1A` | Terminal background |
| `TermFg` | `0x00FF88` | Terminal text (green) |
| `BtnClose` | `0xCC3333` | Red close button |
| `BtnMin` | `0xCCAA00` | Yellow minimize |
| `BtnMax` | `0x33AA33` | Green maximize |

**Primitives**: `fb_fill_rect`, `fb_draw_rect`, `fb_draw_rect_thick`, `fb_draw_line`, `fb_draw_circle`, `fb_fill_circle`, `fb_fill_rect_blend` (alpha blending), `fb_fill_gradient` (vertical linear gradient)

**Text rendering**: Built-in 8×8 bitmap font covering ASCII 32–127. `fb_draw_char`, `fb_draw_text`, `fb_text_width`.

**Change here to**: change any UI color by editing the `Color` namespace constants, add new drawing primitives, swap in a larger font, add horizontal gradients

---

### GUI Layer

#### `gui.cpp` / `gui.h`
The main event loop.
- Parses the **Multiboot v1 framebuffer** fields from `mb_info` at offset 88 (`fb_addr` as `uint64_t`)
- Initializes: `fb_init` → `mouse_init` → `wm_init` → `desktop_init`
- **60 fps loop** using `pit_uptime_ms()` with `hlt` to idle between frames
- Input routing: keyboard → `wm_handle_key`, mouse → `wm_handle_mouse` → taskbar / desktop (if not consumed by WM)
- Render order each frame: `mouse_erase_cursor` → `desktop_draw` → `wm_render_all` → `taskbar_draw` → `startmenu_draw` → `mouse_draw_cursor` → `fb_swap`
- **Change here to**: change frame rate (edit `FRAME_MS`), add global hotkeys (e.g. Alt+Tab), change render order

#### `desktop.cpp` / `desktop.h`
Desktop background, icons, taskbar, and start menu.

**Desktop icons** (`s_icons[]`): Each icon has a label, accent color, glyph index, click callback, position, and hover state. Icons are drawn as 48×48 px boxes with a custom pixel-art glyph inside.

**Taskbar**: Start button (80×28 px, bottom-left) + window buttons (120×24 px each, truncated title, 14-char max) + clock (uptime-based HH:MM:SS) + free RAM display.

**Start menu**: 7 items — Terminal, File Manager, Settings, About, separator, Shutdown, Reboot.

**Change here to**:
- Add a desktop icon → add entry to `s_icons[]` array, increment `ICON_COUNT`, add a glyph case in `draw_icon_glyph`
- Change wallpaper → edit `g_settings.wp_top` / `g_settings.wp_bottom` defaults in `settings_config.cpp`
- Add a start menu item → add entry to `s_menu_items[]` and increment `MENU_ITEM_COUNT`
- Change taskbar height → edit `TASKBAR_HEIGHT` constant in `desktop.h`

#### `splash.cpp` / `splash.h`
Animated boot splash screen. Called once from `gui_run()` before the main event loop.

Sequence:
1. **Starfield** — 300 stars (3 brightness layers) using xorshift32 RNG
2. **CRT glitch** — 10 frames of random horizontal scan-line artifacts
3. **Logo slam-in** — "NodOS" in 5×9 pixel-art bitmaps (8×8 px per pixel block), neon cyan glow with concentric halos, quadratic easing animation from off-screen top
4. **Tagline + version badge**
5. **8 loading steps** — ASCII spinner (`- \ | /`), ellipsis dots, per-step progress
6. **Segmented progress bar** — leading-edge glow effect
7. **Iris-wipe fade** — quadratic rectangle inset converging to black, then desktop color

**Change here to**:
- Skip splash entirely → remove `splash_show()` call in `gui.cpp`
- Change loading step labels/timing → edit `STEPS[]` array in `splash.cpp`
- Change logo color → edit `LOGO_CORE[]`, `LOGO_HALO1[]`, `LOGO_HALO2[]` arrays
- Change pixel art glyphs → edit `GLYPH_N`, `GLYPH_o`, `GLYPH_d`, `GLYPH_O`, `GLYPH_S` bitmaps
- Change logo size → edit `PIXEL_S` constant (currently 8)
- Change animation speed → edit `delay_ms` calls and `OFF[]` array in `animate_logo_in`

---

### Window Manager

#### `wm.cpp` / `wm.h`
Complete window manager with Z-order, chrome, and input dispatch.

**Window chrome** (per window):
- 2px border (`WM_BORDER`)
- 26px title bar (`WM_TITLE_HEIGHT`) with gradient
- Three 14×14 px circular buttons: close (red X), maximize (green square), minimize (yellow dash)
- Client area filled with `Color::WinBody`

**Z-order**: `s_zorder[]` array, index 0 = topmost. `wm_raise` moves a slot to front.

**Drag**: Title bar click+hold starts drag. Mouse offset from window origin is preserved (`s_drag_ox`, `s_drag_oy`). Window is clamped so at least 40px stays on screen.

**Maximize**: Saves `saved_x/y/w/h`, expands to `FB_WIDTH × (FB_HEIGHT - TASKBAR_HEIGHT)`.

**Change here to**:
- Change window chrome colors → edit color constants used in `draw_chrome()`
- Change title bar height → edit `WM_TITLE_HEIGHT` in `wm.h`
- Change button size → edit `WM_BTN_SIZE` in `wm.h`
- Add window resizing → implement hit-testing for border edges in `wm_handle_mouse`
- Change max windows → edit `WM_MAX_WINDOWS` in `wm.h`
- Add window animations → wrap `wm_show`/`wm_hide` with a tween loop

---

### Applications

#### `gui_terminal.cpp` / `gui_terminal.h`
Graphical terminal emulator.
- **400-line scrollback** ring buffer (`TermRow` structs with per-character color)
- Shell output redirected via `gui_terminal_print` → `s_pending` buffer → flushed each frame
- **NodeV integration**: detects `nodev <file>` command, scans source for `pin(varName)` calls, collects inputs interactively before execution
- **vi integration**: detects `vi <file>`, opens `gui_vi_open()` instead of running through shell
- Prompt: `hostname@basename(cwd)> ` with colors (green/grey/cyan/yellow)
- Cursor: block cursor, blinks every second (based on `pit_uptime_s() % 2`)
- **Change here to**: change font size → edit `CHAR_W` / `CHAR_H`, change scrollback lines → edit `SCROLLBACK`, change prompt format → edit `draw_prompt()`, change terminal colors via Settings or `g_settings` defaults

#### `gui_filemanager.cpp` / `gui_filemanager.h`
Split-pane file manager.
- Left pane: directory listing with colored icons (yellow = dir, blue = file), alternating row shading, scrollbar
- Right pane: file preview (up to 8 KB), or directory item count
- Address bar: `<` back, `>` forward (32-entry history stack)
- Status bar: item count + disk usage (MB)
- Keyboard: `j`/`k` or arrow keys to navigate, Enter to open, ESC to go up, `d` to delete
- Mouse: single click selects + previews, double click opens
- **Change here to**: add file rename (`r` key), add new file creation (`n` key), add copy/paste operations, change pane split width (`SPLIT` constant)

#### `gui_vi.cpp` / `gui_vi.h`
GUI vi-style text editor window.
- Multiple files can be open simultaneously (each gets its own WM window)
- Modes: NORMAL (movement + commands) → INSERT (typing) → COMMAND (`:` commands)
- NORMAL commands: `hjkl`/arrows (move), `x` (delete char), `dd` (delete line), `o`/`O` (open line below/above), `A` (append at end), `G`/`gg` (last/first line), `w`/`b` (word forward/back)
- COMMAND: `:w`, `:q`, `:q!`, `:wq`, `:x`, `:w <filename>`
- **Change here to**: add more vi commands in the NORMAL mode handler, add syntax highlighting by colorizing characters on draw

#### `vi.cpp` / `vi.h`
VGA text-mode vi editor (used when running in `nogui` / shell mode, not in GUI).
- Same modal behavior as `gui_vi` but renders directly to VGA 80×25 text buffer via `vga.write_cell()`
- **Change here to**: same as `gui_vi.cpp` — they share the same command set design

#### `gui_settings.cpp` / `gui_settings.h`
Settings panel.
- Sections: Wallpaper (gradient top/bottom color picker + presets), Terminal (bg/fg/cursor color), System (hostname)
- Changes apply instantly to `g_settings` and are saved to `/settings.cfg` via `settings_save()`
- **Change here to**: add new settings fields — add to `NodSettings` struct, add a UI control in `gui_settings_draw`, load/save in `settings_config.cpp`

---

### Filesystem & Shell

#### `vfs.cpp` / `vfs.h`
Flat-table virtual filesystem backed by ATA PIO disk.
- Up to **128 entries** (`VFS_MAX_ENTRIES`)
- Max **8 KB per file** (`VFS_MAX_FILESIZE`)
- Max **256-char paths** (`VFS_MAX_PATH`)
- Each entry stores: full path, start LBA, sector count, size, used flag, type (file/dir)
- Operations: `vfs_read`, `vfs_write`, `vfs_append`, `vfs_delete`, `vfs_mkdir`, `vfs_rmdir`, `vfs_mv`, `vfs_cp`, `vfs_listdir`
- Path resolution: `vfs_resolve` handles `.` / `..` and relative paths against current working directory
- **Change here to**: increase `VFS_MAX_FILESIZE` (and adjust sector_count calculation), increase `VFS_MAX_ENTRIES`, add file permissions, add timestamps

#### `shell.cpp` / `shell.h`
Interactive kernel shell.
- `shell_run()` — blocking read-eval-print loop (VGA text mode)
- `shell_exec(line)` — parses and dispatches a single command line
- `shell_set_gui_output(true)` — redirects all `shell_print` output to `gui_terminal_print` instead of VGA
- **Built-in commands** include (based on usage patterns throughout codebase):
  - `ls`, `cd`, `pwd`, `cat`, `mkdir`, `rm`, `cp`, `mv`, `touch`
  - `ps` (process list), `clear`, `echo`
  - `vi <file>`, `nodev <file>`
  - `shutdown`, `reboot`
  - `help`
- **Change here to**: add a new command by adding a case in `shell_exec`'s command dispatch

---

### Scripting Engine

#### `nodev.cpp` / `nodev.h`
NodeV — a complete interpreted scripting language for NodOS.

**Runs in a fixed 192 KB arena** (reset on each `nodev_exec` call — no memory leaks).

**Language features**:

| Feature | Syntax |
|---|---|
| Variables | `int x = 10;` `float f = 3.14;` `string s = "hi";` |
| Lists | `list nums;` |
| Conditionals | `if (x > 5) { ... } else { ... }` |
| While loop | `while (x > 0) { x = x - 1; }` |
| For loop | `for (int i = 0, i < 10, i = i + 1) { ... }` |
| Functions | `function add(a, b) { return a + b; }` |
| Output | `pout("result: ", add(3, 4), "\n");` |
| Input | `pin(varName);` |
| Import | `$import "other.nod"` |
| Classes | `class Animal { public: int age; constructor(a) { self.age = a; } }` |
| Objects | `Animal dog = new Animal(3);` `dog.speak();` `delete dog;` |

**GUI integration**: `pin()` calls are intercepted by the terminal, which collects inputs interactively before calling `nodev_set_inputs()` + `nodev_run_file()`.

**Change here to**: add new built-in functions by extending the interpreter's built-in function dispatch table in `nodev.cpp`

---

### Settings

#### `settings_config.cpp` / `settings_config.h`
Runtime configuration persisted to `/settings.cfg`.

**`NodSettings` struct**:

| Field | Type | Default | Controls |
|---|---|---|---|
| `wp_top` | `uint32_t` | `0x1E3A5F` | Desktop gradient top color |
| `wp_bottom` | `uint32_t` | `0x0D1B2A` | Desktop gradient bottom color |
| `wp_name` | `char[64]` | `"Classic"` | Wallpaper preset name |
| `term_bg` | `uint32_t` | `0x0A0F1A` | Terminal background |
| `term_fg` | `uint32_t` | `0x00FF88` | Terminal text color |
| `term_cursor` | `uint32_t` | `0x00FF88` | Terminal cursor color |
| `hostname` | `char[32]` | `"nodos"` | Shell prompt hostname |

File format (`/settings.cfg`):
```
WP_TOP=1E3A5F
WP_BOT=0D1B2A
WP_NAME=Classic
TERM_BG=0A0F1A
TERM_FG=00FF88
TERM_CURSOR=00FF88
HOSTNAME=nodos
```

---

## What Each File Controls (Quick-Change Guide)

| I want to change... | Edit this file | Edit this thing |
|---|---|---|
| Desktop background color | `settings_config.cpp` | `g_settings.wp_top` / `wp_bottom` defaults |
| Desktop background color (runtime) | Settings app or `/settings.cfg` | `WP_TOP` / `WP_BOT` values |
| Terminal text color | `settings_config.cpp` | `g_settings.term_fg` default |
| Terminal background | `settings_config.cpp` | `g_settings.term_bg` default |
| Shell prompt hostname | `settings_config.cpp` | `g_settings.hostname` default |
| Window title bar color | `fb.h` | `Color::WinTitle` / `Color::WinTitleFoc` |
| Window body color | `fb.h` | `Color::WinBody` |
| Window border color | `fb.h` | `Color::WinBorder` |
| Close/Min/Max button colors | `fb.h` | `Color::BtnClose` / `BtnMin` / `BtnMax` |
| Taskbar height | `desktop.h` | `TASKBAR_HEIGHT` |
| Desktop icon size | `desktop.h` | `ICON_SIZE` |
| Desktop icon spacing | `desktop.h` | `ICON_STRIDE_Y` |
| Title bar height | `wm.h` | `WM_TITLE_HEIGHT` |
| Window button size | `wm.h` | `WM_BTN_SIZE` |
| Max open windows | `wm.h` | `WM_MAX_WINDOWS` |
| Boot splash colors | `splash.cpp` | `LOGO_CORE[]` / `LOGO_HALO1[]` arrays |
| Boot splash logo | `splash.cpp` | `GLYPH_*` bitmap arrays |
| Boot splash speed | `splash.cpp` | `delay_ms()` calls, `STEPS[].hold_ms` |
| Boot splash steps | `splash.cpp` | `STEPS[]` array, `STEP_COUNT` |
| Screen resolution | `fb.h` + `grub.cfg` | `FB_WIDTH`/`FB_HEIGHT` + `gfxpayload` |
| Frame rate cap | `gui.cpp` | `FRAME_MS` (currently 16 = ~60fps) |
| Keyboard layout | `keyboard.cpp` | `SCANCODE_MAP[]` / `SCANCODE_SHIFT[]` |
| Mouse cursor shape | `mouse.cpp` | `CURSOR_MASK[][]` bitmap |
| Heap size | `kernel.cpp` | `heap_init(start, SIZE)` second argument |
| Timer frequency | `kernel.cpp` | `pit_init(HZ)` argument |
| Max processes | `process.h` | `MAX_PROCESSES` |
| Process stack size | `process.h` | `PROCESS_STACK` |
| Max VFS files | `vfs.h` | `VFS_MAX_ENTRIES` |
| Max file size | `vfs.h` | `VFS_MAX_FILESIZE` |
| Terminal scrollback | `gui_terminal.cpp` | `SCROLLBACK` constant |
| Terminal font size | `gui_terminal.cpp` | `CHAR_W` / `CHAR_H` |
| File manager split | `gui_filemanager.cpp` | `SPLIT` constant |
| Boot timeout | `grub.cfg` | `set timeout=N` |
| Boot mode default | `grub.cfg` | `set default=0` (0=GUI, 1=Text) |

---

## How to Add New Things

### Adding a Shell Command

Open `shell.cpp`, find the command dispatch block inside `shell_exec()`, and add a new case:

```cpp
// In shell_exec(), inside the if/else chain:

else if (k_strcmp(cmd, "hello") == 0) {
    shell_println("Hello from NodOS!");
}

// With arguments (args[] array is populated by shell_exec's tokenizer):
else if (k_strcmp(cmd, "greet") == 0) {
    if (argc > 1) {
        shell_print("Hello, ");
        shell_println(args[1]);
    } else {
        shell_println("Usage: greet ");
    }
}
```

That's all — `shell_print` / `shell_println` automatically route to VGA or GUI terminal depending on `shell_gui_output_active()`.

---

### Adding a Desktop Icon

In `desktop.cpp`:

**Step 1** — Add your glyph index to the `Glyph` enum:
```cpp
enum Glyph { GLYPH_TERMINAL=0, GLYPH_FOLDER=1, GLYPH_INFO=2, GLYPH_SETTINGS=3, GLYPH_MYAPP=4 };
```

**Step 2** — Add a click handler:
```cpp
static void icon_myapp() { gui_myapp_open(); }
```

**Step 3** — Add to the icons array:
```cpp
static DesktopIcon s_icons[] = {
    // ... existing icons ...
    { "MyApp", Color::Green, GLYPH_MYAPP, icon_myapp, 0, 0, false },
};
static const int ICON_COUNT = 5; // increment this
```

**Step 4** — Draw the glyph in `draw_icon_glyph()`:
```cpp
} else if (glyph == GLYPH_MYAPP) {
    // Draw anything using fb_* primitives inside the 48x48 box at (ox, oy)
    fb_fill_circle(ox + 24, oy + 24, 18, accent);
    fb_draw_text(ox + 18, oy + 20, "M", Color::White);
}
```

---

### Adding a Start Menu Item

In `desktop.cpp`, add to `s_menu_items[]` and increment `MENU_ITEM_COUNT`:

```cpp
static void action_myapp() { gui_myapp_open(); startmenu_toggle(); }

static MenuItem s_menu_items[] = {
    { "Terminal",     Color::Cyan,   action_terminal    },
    { "File Manager", Color::Yellow, action_filemanager },
    { "MyApp",        Color::Green,  action_myapp       }, // <-- add here
    // ...
};
static const int MENU_ITEM_COUNT = 8; // was 7, now 8
```

---

### Adding a New GUI Window / App

**Step 1** — Create header `include/gui_myapp.h`:
```cpp
#pragma once
void gui_myapp_open();
void gui_myapp_draw(int wid, int cx, int cy, int cw, int ch, void* ud);
void gui_myapp_key(int wid, char key, void* ud);
void gui_myapp_mouse(int wid, int cx, int cy, bool left, bool right, void* ud);
void gui_myapp_close(int wid, void* ud);
```

**Step 2** — Create `kernel/gui_myapp.cpp`:
```cpp
#include "../include/gui_myapp.h"
#include "../include/wm.h"
#include "../include/fb.h"
#include "../include/kstring.h"

static int s_wid = -1;

void gui_myapp_draw(int, int cx, int cy, int cw, int ch, void*) {
    fb_fill_rect(cx, cy, cw, ch, Color::WinBody);
    fb_draw_text(cx + 10, cy + 10, "Hello from MyApp!", Color::TextNormal);
}

void gui_myapp_key(int, char key, void*) {
    // Handle key input
}

void gui_myapp_mouse(int, int cx, int cy, bool left, bool, void*) {
    // Handle mouse clicks within client area
}

void gui_myapp_close(int wid, void*) {
    wm_destroy(wid);
    s_wid = -1;
}

void gui_myapp_open() {
    if (s_wid >= 0) { wm_show(s_wid); wm_focus(s_wid); return; }
    s_wid = wm_create("MyApp", 200, 150, 400, 300);
    wm_set_callbacks(s_wid,
                     gui_myapp_draw,
                     gui_myapp_key,
                     gui_myapp_close,
                     gui_myapp_mouse,
                     nullptr);
}
```

**Step 3** — Add `gui_myapp.cpp` to `Makefile` sources.

**Step 4** — Call `gui_myapp_open()` from a desktop icon, start menu item, or shell command.

---

### Adding a New System Call / Kernel Feature

Since NodOS has no user/kernel boundary for applications yet (everything runs in ring 0):

1. Add your function declaration to an appropriate header (or create a new one)
2. Implement in a new `.cpp` file
3. Add the `.cpp` to the Makefile
4. Call from wherever needed (shell, GUI app, etc.)

For a future ring-3 syscall gate:
1. Add a TSS entry to the GDT (`gdt.cpp`)
2. Add an INT 0x80 gate to the IDT (`idt.cpp`, `cpu.asm`)
3. Implement a syscall dispatch table in a new `syscall.cpp`

---

### Adding a New IRQ Handler

Any device that fires a hardware interrupt can be handled in three lines:

```cpp
// 1. Write your handler (in your driver's .cpp)
static void mydevice_irq(Registers* r) {
    (void)r;
    // read data from device port, update state
    uint8_t data = inb(MY_DEVICE_PORT);
    // do something with data
}

// 2. Register it (call this from your init function)
void mydevice_init() {
    irq_install_handler(5, mydevice_irq); // IRQ 5 = INT 37
}

// 3. Call mydevice_init() from kernel_main() in kernel.cpp
```

Available IRQs: 0=timer (taken), 1=keyboard (taken), 2=cascade, 3=COM2, 4=COM1, 5=LPT2/sound, 6=floppy, 7=LPT1, 8=RTC, 9=ACPI, 10=free, 11=free, 12=mouse (taken), 13=FPU, 14=ATA primary (taken), 15=ATA secondary.

---

### Adding a New NodeV Built-in Function

In `nodev.cpp`, find the built-in function dispatcher (where `pout` and `pin` are handled) and add a new case:

```cpp
// Inside the built-in call handler:
if (k_strcmp(func_name, "strlen") == 0) {
    // args[0] is the first argument (already evaluated to a Value)
    int len = k_strlen(args[0].str_val);
    result.type    = VAL_INT;
    result.int_val = len;
    return result;
}
```

Then you can use it in any `.nod` script:
```
string s = "hello";
int n = strlen(s);
pout("Length: ", n, "\n");
```

---

### Adding a New Settings Option

**Step 1** — Add field to `NodSettings` in `settings_config.h`:
```cpp
struct NodSettings {
    // ... existing fields ...
    uint32_t icon_color;   // new: desktop icon accent color
};
```

**Step 2** — Set default in `settings_config.cpp`:
```cpp
NodSettings g_settings = {
    // ... existing ...
    /* icon_color */ 0x44FF88,
};
```

**Step 3** — Add save/load in `settings_config.cpp`:
```cpp
// In settings_save():
fmt_hex6(g_settings.icon_color, hex);
append_kv(buf, "ICON_COLOR", hex);

// In settings_load():
if (k_strcmp(key, "ICON_COLOR") == 0) g_settings.icon_color = parse_hex(val);
```

**Step 4** — Add a UI control in `gui_settings.cpp` (draw a color swatch + click handler).

**Step 5** — Use `g_settings.icon_color` wherever you need it.

---

### Adding a New VFS File at Boot

In `vfs_init()` inside `vfs.cpp`, after the filesystem table is loaded, create default files if they don't exist:

```cpp
// At the end of vfs_init():
if (vfs_exists("/etc/motd") < 0) {
    vfs_mkdir("/etc");
    vfs_write("/etc/motd", "Welcome to NodOS!\n", 18);
}
```

Or from the shell at runtime:
```
touch /home/hello.nod
```
Then edit with `vi /home/hello.nod`.

---

### Adding a New Process / Background Task

```cpp
// 1. Write your task function (must never return, or call process_kill)
void my_background_task() {
    while (true) {
        // do work
        do_something();
        // yield to other processes
        process_sleep(1000);  // sleep 1 second
    }
}

// 2. Spawn it from kernel_main (after process_init):
process_create("mytask", my_background_task);

// 3. It will appear in `ps` output and be scheduled round-robin
```

For a display-only entry in `ps` (no actual execution):
```cpp
process_record_dummy("networking");
```

---

## Customization Reference

### Colors & Theme

All UI colors live in `namespace Color` in `fb.h`. Change any constant to immediately affect the entire UI:

```cpp
namespace Color {
    static const uint32_t WinBody     = 0x0E1E30;  // window background — change this for a lighter theme
    static const uint32_t Desktop     = 0x1E3A5F;  // static desktop bg (overridden by g_settings at runtime)
    static const uint32_t TermFg      = 0x00FF88;  // default terminal green — change for amber/white
    static const uint32_t Taskbar     = 0x0F1F35;  // taskbar — try 0x1A1A2E for a purple theme
}
```

For a **light theme**, change:
- `WinBody` → `0xF0F0F0`
- `WinTitle` → `0xC8D8E8`
- `TextNormal` → `0x101010`
- `Taskbar` → `0xD0D8E0`
- `Desktop` / `wp_top` / `wp_bottom` → light grays

---

### Screen Resolution

**1.** Change `grub.cfg`:
```
set gfxpayload=800x600x32,800x600
```

**2.** Change `fb.h`:
```cpp
static const uint32_t FB_WIDTH  = 800;
static const uint32_t FB_HEIGHT = 600;
```

**3.** Rebuild. All layout constants that reference `FB_WIDTH`/`FB_HEIGHT` (taskbar, splash, progress bar, etc.) adapt automatically.

---

### Boot Splash

To **disable** the splash:
```cpp
// In gui.cpp, gui_run():
// splash_show();   ← comment out
```

To change the **step count and labels** (`splash.cpp`):
```cpp
static const LoadStep STEPS[] = {
    { "My custom step one",   200, 33 },
    { "My custom step two",   200, 66 },
    { "Ready",                 50, 100 },
};
static constexpr int STEP_COUNT = 3;
```

---

### Terminal Behavior

| What | Where | How |
|---|---|---|
| Scrollback lines | `gui_terminal.cpp` | Change `SCROLLBACK` (currently 400) |
| Max columns | `gui_terminal.cpp` | Change `MAX_COLS` (currently 120) |
| Font width/height | `gui_terminal.cpp` | Change `CHAR_W` / `CHAR_H` (currently 8 / 14) |
| Cursor blink speed | `gui_terminal.cpp` | Change `pit_uptime_s() % 2` (2 = 0.5Hz) |
| Prompt format | `gui_terminal.cpp` | Edit `draw_prompt()` function |
| Default colors | `settings_config.cpp` | Edit `g_settings.term_bg` / `term_fg` |

---

### Keyboard Layout

To change the layout, edit the two tables in `keyboard.cpp`:

```cpp
static const char SCANCODE_MAP[128] = {
    // Index = PS/2 scancode set 1
    // Change characters here to remap keys
    0, 27, '1', '2', ...
};

static const char SCANCODE_SHIFT[128] = {
    // Same indices, shifted versions
    0, 27, '!', '@', ...
};
```

---

### Wallpaper Gradient

Runtime (persists across reboots): Use the **Settings** app inside NodOS.

Hardcoded default (before settings load): Edit `settings_config.cpp`:
```cpp
NodSettings g_settings = {
    /* wp_top    */ 0x2D1B69,   // deep purple
    /* wp_bottom */ 0x11052C,   // near black purple
    // ...
};
```

Solid color desktop: Set both `wp_top` and `wp_bottom` to the same value.

---

### Hostname & Prompt

Runtime: Use the **Settings** app (hostname field) or edit `/settings.cfg` directly.

Default: Edit `settings_config.cpp`:
```cpp
/* hostname */ "mypc"
```

---

## NodeV Scripting Language

Scripts are plain text files with `.nod` extension stored in the VFS. Run with `nodev <filename>` from the terminal.

### Hello World

```javascript
pout("Hello, NodOS!\n");
```

### Variables & Arithmetic

```javascript
int x = 42;
float pi = 3.14159;
string name = "World";
pout("Hello, ", name, "! x = ", x, "\n");
```

### Control Flow

```javascript
int n = 10;
if (n > 5) {
    pout("big\n");
} else {
    pout("small\n");
}

while (n > 0) {
    pout(n, " ");
    n = n - 1;
}
pout("\n");

for (int i = 0, i < 5, i = i + 1) {
    pout(i, "\n");
}
```

### Functions

```javascript
function factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}
pout(factorial(6), "\n");
```

### Classes (OOP)

```javascript
class Dog {
    public:
    string name;
    int age;
    constructor(n, a) {
        self.name = n;
        self.age = a;
    }
    function bark() {
        pout(self.name, " says: Woof!\n");
    }
}

Dog rex = new Dog("Rex", 3);
rex.bark();
pout(rex.name, " is ", rex.age, " years old\n");
delete rex;
```

### User Input (GUI terminal intercepts `pin()`)

```javascript
string username;
pin(username);
pout("Hello, ", username, "!\n");
```

### File Import

```javascript
$import "utils.nod"
greet("NodOS");    // function defined in utils.nod
```

---

## Known Limitations

- **No virtual memory / paging** — all code runs in physical memory, ring 0
- **No user space** — no protection between processes
- **Single core only** — no SMP support
- **ATA PIO only** — no DMA, no AHCI/NVMe
- **Flat VFS** — no journaling, no permissions, max 128 files, max 8 KB per file
- **No network stack**
- **No audio**
- **No floating point in kernel** — FPU not initialized (affects NodeV floats on some hardware)
- **No ACPI** — shutdown only works in QEMU/VirtualBox/Bochs via magic ports
- **8×8 font only** — no TrueType, no Unicode, ASCII 32–127 only
- **NodeV arena resets** between scripts — no persistent global state across `nodev_exec` calls

---

## Toolchain & Dependencies

### Setting Up the Cross-Compiler

```bash
# Build binutils for i686-elf target
wget https://ftp.gnu.org/gnu/binutils/binutils-2.41.tar.gz
tar xf binutils-2.41.tar.gz
mkdir build-binutils && cd build-binutils
../binutils-2.41/configure --target=i686-elf --with-sysroot --disable-nls --disable-werror
make && sudo make install

# Build GCC for i686-elf target
wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.gz
tar xf gcc-13.2.0.tar.gz
mkdir build-gcc && cd build-gcc
../gcc-13.2.0/configure --target=i686-elf --disable-nls --enable-languages=c,c++ --without-headers
make all-gcc && make all-target-libgcc
sudo make install-gcc && sudo make install-target-libgcc
```

### Required Packages (Ubuntu/Debian)

```bash
sudo apt install nasm qemu-system-i386 xorriso grub-pc-bin grub-common
```

### Required Packages (Arch Linux)

```bash
sudo pacman -S nasm qemu xorriso grub
```

### QEMU Run Command (equivalent to `make run`)

```bash
qemu-system-i386 \
  -cdrom nodos.iso \
  -m 256M \
  -drive file=disk.img,format=raw,if=ide \
  -vga std \
  -display sdl
```

---

*NodOS v4.0 — Built with pure C++ + NASM, no STL, no libc, no shortcuts.*
