#include "../include/process.h"
#include "../include/heap.h"
#include "../include/kstring.h"
#include "../include/pit.h"

static Process procs[MAX_PROCESSES];
int current_pid = 0;

// Build the initial kernel-thread stack frame that irq_common expects:
// ds, edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax,
// int_no, err_code, eip, cs, eflags  (14 dwords = 56 bytes)
static void setup_stack(Process* p, void (*entry)()) {
    uint32_t* sp = (uint32_t*)(p->stack + PROCESS_STACK);

    *--sp = 0x202;              // EFLAGS (interrupts enabled)
    *--sp = 0x08;               // CS (kernel code selector)
    *--sp = (uint32_t)entry;    // EIP
    *--sp = 0;                  // err_code
    *--sp = 0;                  // int_no
    // pusha frame (eax last pushed = highest addr, edi = lowest)
    *--sp = 0;  // eax
    *--sp = 0;  // ecx
    *--sp = 0;  // edx
    *--sp = 0;  // ebx
    *--sp = 0;  // esp (dummy, ignored by popa)
    *--sp = 0;  // ebp
    *--sp = 0;  // esi
    *--sp = 0;  // edi
    *--sp = 0x10; // ds (kernel data selector)

    p->esp = (uint32_t)sp;
}

void process_init() {
    k_memset(procs, 0, sizeof(procs));

    // PID 1: the kernel/shell (already running — no stack setup needed)
    procs[0].pid    = 1;
    procs[0].state  = PROC_RUNNING;
    k_strcpy(procs[0].name, "shell");
    procs[0].stack  = nullptr;   // uses the real kernel stack
    procs[0].esp    = 0;         // filled in by scheduler_tick on first preemption
    current_pid     = 1;
}

int process_create(const char* name, void (*entry)()) {
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (procs[i].state == PROC_UNUSED) {
            procs[i].pid   = i + 1;
            procs[i].state = PROC_READY;
            k_strncpy(procs[i].name, name, 31);
            procs[i].stack = (uint8_t*)kmalloc(PROCESS_STACK);
            if (!procs[i].stack) return -1;
            setup_stack(&procs[i], entry);
            return procs[i].pid;
        }
    }
    return -1;
}

void process_kill(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (procs[i].pid == pid && procs[i].state != PROC_UNUSED) {
            procs[i].state = PROC_DEAD;
            if (procs[i].stack) { kfree(procs[i].stack); procs[i].stack = nullptr; }
            if (pid == current_pid) {
                // Will be cleaned up on next scheduler tick
                __asm__ volatile("hlt");
            }
            return;
        }
    }
}

void process_sleep(uint32_t ms) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (procs[i].pid == current_pid) {
            procs[i].state       = PROC_SLEEPING;
            procs[i].sleep_until = pit_ticks() + (ms * 100 / 1000) + 1;
            __asm__ volatile("hlt");  // wait for next reschedule
            return;
        }
    }
}

void process_yield() {
    __asm__ volatile("hlt");
}

Process* process_get(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (procs[i].pid == pid) return &procs[i];
    return nullptr;
}

void process_list(Process** out, int* count) {
    *count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (procs[i].state != PROC_UNUSED && procs[i].state != PROC_DEAD)
            out[(*count)++] = &procs[i];
}

// Called from idt.cpp irq_handler on every timer tick.
// Returns the ESP of the task to switch to (may be same or different).
uint32_t scheduler_tick(uint32_t current_esp) {
    uint32_t now = pit_ticks();

    // Find current process slot and save its ESP
    int cur_slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (procs[i].pid == current_pid) {
            cur_slot = i;
            if (procs[i].state == PROC_RUNNING) {
                procs[i].esp   = current_esp;
                procs[i].state = PROC_READY;
            }
            break;
        }
    }

    // Wake up sleeping processes
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (procs[i].state == PROC_SLEEPING && now >= procs[i].sleep_until)
            procs[i].state = PROC_READY;
        // Clean up dead processes
        if (procs[i].state == PROC_DEAD) {
            if (procs[i].stack) { kfree(procs[i].stack); procs[i].stack = nullptr; }
            procs[i].state = PROC_UNUSED;
            procs[i].pid   = 0;
        }
    }

    // Round-robin: find next READY process
    if (cur_slot < 0) return current_esp;

    for (int off = 1; off <= MAX_PROCESSES; off++) {
        int next = (cur_slot + off) % MAX_PROCESSES;
        if (procs[next].state == PROC_READY) {
            procs[next].state = PROC_RUNNING;
            current_pid = procs[next].pid;
            return procs[next].esp;
        }
    }

    // No other ready process — resume current
    if (cur_slot >= 0 && procs[cur_slot].state == PROC_READY) {
        procs[cur_slot].state = PROC_RUNNING;
    }
    return current_esp;
}
