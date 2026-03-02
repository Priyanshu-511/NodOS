; boot.asm — Multiboot entry point with framebuffer request
;
; Multiboot flags:
;   bit 0 = page-align modules
;   bit 1 = provide memory map
;   bit 2 = provide video mode info  ← NEW (request framebuffer)
;
; The video mode fields below ask for 1024×768×32bpp linear framebuffer.
; GRUB/QEMU will set it up before calling _start.

MBALIGN   equ  1 << 0
MEMINFO   equ  1 << 1
VIDEOMODE equ  1 << 2       ; ← request video mode info

FLAGS     equ  MBALIGN | MEMINFO | VIDEOMODE
MAGIC     equ  0x1BADB002
CHECKSUM  equ  -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

    ; Fields used only when bit 16 set (load address — we use linker for this):
    dd 0, 0, 0, 0, 0

    ; Video mode fields (Multiboot spec §3.1.3) — used when bit 2 is set:
    ;   mode_type 0 = linear framebuffer, 1 = EGA text
    dd 0            ; mode_type  (0 = graphics / linear framebuffer)
    dd 1024         ; width
    dd 768          ; height
    dd 32           ; depth (bits per pixel)

section .bss
align 16
global stack_bottom
global stack_top
stack_bottom:
    resb 32768          ; 32 KB kernel stack (doubled for GUI)
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov  esp, stack_top
    push ebx            ; multiboot info pointer (mb_info)
    push eax            ; multiboot magic
    call kernel_main
    cli
.hang:
    hlt
    jmp .hang