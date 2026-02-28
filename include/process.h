#pragma once
#include <stdint.h>

static const int MAX_PROCESSES   = 16;
static const int PROCESS_STACK   = 8192;   // 8 KB per process

enum ProcessState {
    PROC_UNUSED  = 0,
    PROC_READY   = 1,
    PROC_RUNNING = 2,
    PROC_SLEEPING = 3,
    PROC_DEAD    = 4,
};

struct Process {
    int          pid;
    char         name[32];
    ProcessState state;
    uint32_t     esp;          // saved stack pointer
    uint8_t*     stack;        // kernel stack base
    uint32_t     sleep_until;  // tick count to wake up at
};

void     process_init();
int      process_create(const char* name, void (*entry)());
void     process_kill(int pid);
void     process_sleep(uint32_t ms);
void     process_yield();
Process* process_get(int pid);
void     process_list(Process** out, int* count);

// Called from timer IRQ — returns new ESP (may switch tasks)
uint32_t scheduler_tick(uint32_t current_esp);

extern int current_pid;
