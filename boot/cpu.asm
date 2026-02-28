; cpu.asm — GDT/IDT flush, ISR/IRQ stubs
bits 32
section .text


;  GDT flush — reload all segment registers

global gdt_flush
gdt_flush:
    mov eax, [esp+4]
    lgdt [eax]
    mov ax, 0x10        ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.done      ; far jump reloads CS with kernel code selector
.done:
    ret


;  IDT load

global idt_load
idt_load:
    mov eax, [esp+4]
    lidt [eax]
    ret


;  ISR stubs (exceptions 0-31)
;  Some push a real error code; others push a dummy 0 first.

extern isr_handler

%macro ISR_NO_ERR 1
global isr%1
isr%1:
    push dword 0        ; dummy error code
    push dword %1       ; interrupt number
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1       ; interrupt number (CPU already pushed real err code)
    jmp isr_common
%endmacro

ISR_NO_ERR 0   ; Divide-by-zero
ISR_NO_ERR 1   ; Debug
ISR_NO_ERR 2   ; NMI
ISR_NO_ERR 3   ; Breakpoint
ISR_NO_ERR 4   ; Overflow
ISR_NO_ERR 5   ; Bound range exceeded
ISR_NO_ERR 6   ; Invalid opcode
ISR_NO_ERR 7   ; Device not available
ISR_ERR    8   ; Double fault
ISR_NO_ERR 9   ; Coprocessor segment overrun
ISR_ERR    10  ; Invalid TSS
ISR_ERR    11  ; Segment not present
ISR_ERR    12  ; Stack-segment fault
ISR_ERR    13  ; General protection fault
ISR_ERR    14  ; Page fault
ISR_NO_ERR 15
ISR_NO_ERR 16  ; x87 FP exception
ISR_ERR    17  ; Alignment check
ISR_NO_ERR 18  ; Machine check
ISR_NO_ERR 19  ; SIMD FP exception
ISR_NO_ERR 20
ISR_NO_ERR 21
ISR_NO_ERR 22
ISR_NO_ERR 23
ISR_NO_ERR 24
ISR_NO_ERR 25
ISR_NO_ERR 26
ISR_NO_ERR 27
ISR_NO_ERR 28
ISR_NO_ERR 29
ISR_ERR    30
ISR_NO_ERR 31

isr_common:
    pusha                   ; saves eax,ecx,edx,ebx,esp,ebp,esi,edi
    mov ax, ds
    push eax                ; save DS
    mov ax, 0x10            ; load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                ; pointer to saved registers (Registers*)
    call isr_handler
    add esp, 4              ; remove argument
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8              ; remove int_no + err_code
    iret


;  IRQ stubs (IRQ 0-15 → INT 32-47)
;  irq_handler returns a uint32_t: the ESP to use after the
;  interrupt.  This allows the scheduler to switch stacks.

extern irq_handler

%macro IRQ 2
global irq%1
irq%1:
    push dword 0            ; dummy error code
    push dword %2           ; interrupt number
    jmp irq_common
%endmacro

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

irq_common:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                ; arg: current ESP (= pointer to saved state)
    call irq_handler
    ; eax = new ESP (scheduler may have changed it)
    mov esp, eax
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    iret
