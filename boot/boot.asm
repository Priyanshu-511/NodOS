; boot.asm — Multiboot entry point
MBALIGN  equ 1 << 0
MEMINFO  equ 1 << 1
FLAGS    equ MBALIGN | MEMINFO
MAGIC    equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 16
global stack_bottom
global stack_top
stack_bottom:
    resb 16384          ; 16 KB kernel stack
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top
    push ebx            ; multiboot info pointer
    push eax            ; multiboot magic
    call kernel_main
    cli
.hang:
    hlt
    jmp .hang
