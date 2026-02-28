# NodOS — Developer Workflow Guide

How to add anything new to NodOS: a driver, a shell command, a subsystem, or a full new phase.  
Follow these steps every time.

---

## Table of Contents

- [The Golden Rule](#the-golden-rule)
- [Step 1 — Plan Before You Code](#step-1--plan-before-you-code)
- [Step 2 — Create the Header File](#step-2--create-the-header-file)
- [Step 3 — Write the Implementation](#step-3--write-the-implementation)
- [Step 4 — Register with the Kernel](#step-4--register-with-the-kernel)
- [Step 5 — Add to the Makefile](#step-5--add-to-the-makefile)
- [Step 6 — Expose in the Shell (optional)](#step-6--expose-in-the-shell-optional)
- [Step 7 — Build and Test](#step-7--build-and-test)
- [Step 8 — Debug a Blank Screen or Triple Fault](#step-8--debug-a-blank-screen-or-triple-fault)
- [Common Patterns](#common-patterns)
  - [Adding a shell command](#adding-a-shell-command)
  - [Adding a hardware driver (IRQ-based)](#adding-a-hardware-driver-irq-based)
  - [Adding a new subsystem](#adding-a-new-subsystem)
- [Things That Will Break You (and how to avoid them)](#things-that-will-break-you-and-how-to-avoid-them)
- [Quick Reference](#quick-reference)

---

## The Golden Rule

> **Never rely on C++ global constructors.**  
> In a freestanding kernel, `.init_array` is never walked so constructors of global objects silently never run.  
> Always use an explicit `foo_init()` function and call it from `kernel_main()`.

This is the single most common cause of blank screens and mysterious crashes.

---

## Step 1 — Plan Before You Code

Before touching any file, answer these four questions:

1. **What does it need from the kernel?**  
   e.g. "My RTC driver needs `inb`/`outb` from `io.h` and wants to install an IRQ handler via `idt.h`."

2. **What does it provide to the rest of the kernel?**  
   e.g. "It exposes `rtc_read_time()` returning hour, minute, second."

3. **When in the boot sequence must it be initialised?**  
   e.g. "After IDT (so IRQs work) but before the shell."

4. **Does it need dynamic memory (`kmalloc`)?**  
   If yes, it must be initialised after `heap_init()`.

Write the answers down — they directly determine what goes in the header and where `foo_init()` is called.

---

## Step 2 — Create the Header File

Every new component gets its own header in `include/`.

```
include/foo.h
```

### Template

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

// ── Public API ───────────────────────────────────────────────
// Brief description of what this module does.

void     foo_init();            // always provide an init function
uint32_t foo_do_something();    // whatever your module exposes
```

### Rules for headers

- Use `#pragma once` — not `#ifndef` guards (cleaner, works fine here)
- Only declare things that other files need; keep internals in the `.cpp`
- Never define variables or write function bodies in a header (except `inline` helpers)
- Never `#include` a `.cpp` file

---

## Step 3 — Write the Implementation

Create the source file in `kernel/`:

```
kernel/foo.cpp
```

### Template

```cpp
#include "../include/foo.h"
#include "../include/vga.h"       // for debug output
#include "../include/io.h"        // if you need port I/O
#include "../include/kstring.h"   // if you need string helpers
// add other includes as needed

// ── Private state ────────────────────────────────────────────
static uint32_t foo_counter = 0;   // static = private to this file

// ── Private helpers ──────────────────────────────────────────
static void foo_helper() {
    // ...
}

// ── Public API ───────────────────────────────────────────────
void foo_init() {
    foo_counter = 0;
    // do your setup here
    vga.setColor(LIGHT_GREEN, BLACK); vga.print("  [ OK ] ");
    vga.setColor(LIGHT_GREY,  BLACK); vga.println("foo    ready");
}

uint32_t foo_do_something() {
    return foo_counter++;
}
```

### Rules for implementation files

- Mark every internal variable and function `static` — this prevents name collisions with other files
- Never use `new` / `delete` — use `kmalloc` / `kfree` from `heap.h`
- Never use `printf`, `malloc`, `memcpy` etc. — use `vga.print`, `kmalloc`, `k_memcpy` from `kstring.h`
- No C++ exceptions, no RTTI, no STL — the compiler flags `-fno-exceptions -fno-rtti` forbid them
- If you need string operations, use the `k_` prefixed functions from `kstring.h`

---

## Step 4 — Register with the Kernel

Open `kernel/kernel.cpp` and add your `foo_init()` call in the right place inside `kernel_main()`.

```cpp
// kernel/kernel.cpp

#include "../include/foo.h"    // ← add this include at the top

extern "C" void kernel_main(uint32_t magic, uint32_t mb_info) {

    vga.init();          // Phase 1  — must be first
    gdt_init();          // Phase 2
    idt_init();          //
    pmm_init(...);       //
    heap_init(...);      //
    pit_init(100);       // Phase 3
    keyboard_init();     //
    __asm__ volatile("sti");

    foo_init();          // ← add here, in the right order
                         //   (after anything foo depends on)

    process_init();      // Phase 5
    vfs_init();          // Phase 4
    shell_init();        //
    shell_run();         // never returns
}
```

**Boot order cheat sheet** — a module must come *after* everything it depends on:

```
vga.init()          ← always first, everything uses it
gdt_init()          ← required before idt
idt_init()          ← required before any IRQ handler
pmm_init()          ← required before heap
heap_init()         ← required before anything that calls kmalloc
pit_init()          ← IRQ 0 handler
keyboard_init()     ← IRQ 1 handler
sti                 ← enable interrupts only after PIC + IDT are ready
process_init()      ← requires heap (for process stacks)
vfs_init()          ← no dependencies
shell_init()        ← requires vfs
shell_run()         ← last — never returns
```

---

## Step 5 — Add to the Makefile

Open `Makefile` and add your object file to the `OBJS` list:

```makefile
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
    $(BUILD_DIR)/vfs.o       \
    $(BUILD_DIR)/process.o   \
    $(BUILD_DIR)/foo.o       \   # ← add this line
    $(BUILD_DIR)/shell.o     \
    $(BUILD_DIR)/kernel.o        # kernel.o always last
```

The wildcard build rule `$(BUILD_DIR)/%.o: kernel/%.cpp` already handles any new `.cpp` file you drop in `kernel/` — no extra rule needed.

---

## Step 6 — Expose in the Shell (optional)

If your feature should be user-accessible, add a command to `kernel/shell.cpp`.

### 1. Add a `cmd_foo()` function

```cpp
// kernel/shell.cpp
#include "../include/foo.h"    // ← add include at top

static void cmd_foo(char* argv[], int argc) {
    if (argc < 2) {
        vga.println("Usage: foo <argument>");
        return;
    }
    uint32_t result = foo_do_something();
    vga.print("Result: "); vga.printUInt(result); vga.putChar('\n');
}
```

### 2. Register it in `shell_exec()`

```cpp
void shell_exec(const char* line) {
    // ... existing commands ...

    if (k_strcmp(argv[0], "foo") == 0) { cmd_foo(argv, argc); return; }

    // ... unknown command handler ...
}
```

### 3. Add it to `cmd_help()`

```cpp
static void cmd_help() {
    // ... existing entries ...
    vga.println("  foo <arg>          Description of what foo does");
}
```

---

## Step 7 — Build and Test

```bash
# Full clean build (recommended when adding new files)
make clean && make

# If only editing existing files (incremental)
make

# Boot and test
make run
```

If the build succeeds, GRUB should load and you should see the boot messages in order. Test your feature interactively from the shell.

---

## Step 8 — Debug a Blank Screen or Triple Fault

A blank/black screen after GRUB almost always means the kernel crashed before `vga.init()` printed anything. Work through this checklist:

### Checklist

**1. Did you call `vga.init()` first?**  
It must be the very first line of `kernel_main`. Without it, `vga.buffer` is `nullptr` and every print writes to address 0x00000000 — triple fault.

**2. Did you dereference a null or invalid pointer?**  
Any pointer that wasn't explicitly initialised is zeroed in BSS. Calling through it = triple fault.  
Use `if (ptr) { ... }` guards or ensure init order is correct.

**3. Did you call `kmalloc` before `heap_init()`?**  
`heap_start` will be null, `kmalloc` returns null, next use crashes.

**4. Did you install an IRQ/ISR handler before `idt_init()`?**  
The IDT doesn't exist yet — writing to it corrupts memory.

**5. Did you enable interrupts (`sti`) before the IDT and PIC were set up?**  
A spurious interrupt with no handler = triple fault.

**6. Did you write past the end of a stack-allocated buffer?**  
The kernel stack is only 16 KB. Large arrays on the stack (`char buf[16384]`) will overflow it.  
Use `static` local variables for large buffers, or `kmalloc`.

**7. Is your new `.o` file in `OBJS` in the Makefile?**  
If not, your `foo_init()` will be an undefined symbol and the link will fail — or worse, link silently if it's called through a wrong address.

### Add a "breadcrumb" print

If you're not sure where the crash happens, add temporary `vga.println()` calls:

```cpp
vga.println("before foo_init");
foo_init();
vga.println("after foo_init");    // if this doesn't appear, foo_init crashed
```

Remove them once you find the fault.

### Use QEMU's monitor

```
Ctrl+A  C           # switch to QEMU monitor
info registers      # dump CPU registers — EIP shows where it crashed
info mem            # show page table (if paging is enabled)
x /10i $eip         # disassemble 10 instructions at current EIP
q                   # quit
```

---

## Common Patterns

### Adding a shell command

Fastest path — no new files needed:

1. Add `#include "../include/whatever.h"` at the top of `shell.cpp` if needed
2. Write a `static void cmd_name(char* argv[], int argc)` function in `shell.cpp`
3. Add `if (k_strcmp(argv[0], "name") == 0) { cmd_name(argv, argc); return; }` in `shell_exec()`
4. Add a line to `cmd_help()`
5. `make && make run`

---

### Adding a hardware driver (IRQ-based)

Example: adding an RTC driver on IRQ 8.

**1.** Create `include/rtc.h` and `kernel/rtc.cpp`

**2.** In `rtc.cpp`, write your IRQ handler and register it:

```cpp
#include "../include/idt.h"
#include "../include/io.h"

static void rtc_irq_handler(Registers*) {
    // read RTC registers via port I/O
    inb(0x71);      // must read register C to re-arm RTC interrupt
}

void rtc_init() {
    // enable IRQ 8 in RTC (CMOS register B, bit 6)
    outb(0x70, 0x8B);
    uint8_t prev = inb(0x71);
    outb(0x70, 0x8B);
    outb(0x71, prev | 0x40);

    irq_install_handler(8, rtc_irq_handler);   // IRQ 8 = slave PIC pin 0
}
```

**3.** Call `rtc_init()` in `kernel_main()` after `idt_init()` and after `sti`

**4.** Add `$(BUILD_DIR)/rtc.o` to `OBJS` in the Makefile

---

### Adding a new subsystem

Example: a circular log buffer that stores the last N kernel messages.

**1.** `include/klog.h` — declare `klog_init()`, `klog_write(const char*)`, `klog_dump()`

**2.** `kernel/klog.cpp` — implement with a static ring buffer (no `kmalloc` needed for fixed-size logs)

**3.** `kernel/kernel.cpp` — add `#include "../include/klog.h"` and `klog_init()` early in `kernel_main`

**4.** `kernel/shell.cpp` — add a `log` command that calls `klog_dump()`

**5.** `Makefile` — add `$(BUILD_DIR)/klog.o` to `OBJS`

**6.** Optionally, call `klog_write(msg)` anywhere in the kernel to record events

---

## Things That Will Break You (and how to avoid them)

| Mistake | Symptom | Fix |
|---------|---------|-----|
| Global object with non-trivial constructor | Silent blank screen — constructor never ran | Replace with `static Type obj;` + explicit `obj.init()` |
| Large array on the kernel stack | Random crash / stack overflow | Declare it `static` inside the function or use `kmalloc` |
| Calling `kmalloc` before `heap_init()` | Returns null, next use crashes | Move call after `heap_init()` in boot order |
| IRQ handler installed before `idt_init()` | Memory corruption | Always install handlers after `idt_init()` |
| `sti` before PIC is remapped | Spurious interrupt → triple fault | `sti` must come after both `idt_init()` (which remaps PIC) and `pit_init()` |
| Forgetting to add `.o` to `OBJS` | Undefined symbol linker error | Add `$(BUILD_DIR)/yourfile.o` to `OBJS` in Makefile |
| Writing `vga->method()` instead of `vga.method()` | Compiler error: cannot convert VGADriver to bool | `vga` is a value, not a pointer — use `.` not `->` |
| Using `new` / `delete` | Linker error: undefined `operator new` | Use `kmalloc` / `kfree` from `heap.h` |
| Using `std::` anything | Linker or compiler error | No STL in freestanding — write your own or use `kstring.h` |
| Returning from `shell_run()` | CPU falls off the end of `kernel_main` into garbage memory | `shell_run()` loops forever — never return from it |

---

## Quick Reference

```
Adding a new feature? Run through this checklist:

[ ] include/foo.h        created — declares init + public API only
[ ] kernel/foo.cpp       created — all internals marked static
[ ] kernel/kernel.cpp    #include "../include/foo.h" added
[ ] kernel/kernel.cpp    foo_init() called in correct boot order
[ ] Makefile             $(BUILD_DIR)/foo.o added to OBJS
[ ] kernel/shell.cpp     cmd_foo() + shell_exec() entry + help line  (if needed)
[ ] make clean && make   builds without errors or warnings
[ ] make run             boots, no blank screen, feature works
```