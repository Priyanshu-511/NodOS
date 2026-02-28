#include "../include/keyboard.h"
#include "../include/idt.h"
#include "../include/io.h"
#include "../include/vga.h"

static const char SCANCODE_MAP[128] = {
    0,   27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,        // F1-F10
    0,  // Num lock
    0,  // Scroll lock
    0,'8',0,'+','2',0,'0',127,  // keypad
    0,0,0,
    0,0                         // F11,F12
};

static const char SCANCODE_SHIFT[128] = {
    0,   27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,
    0,'8',0,'+','2',0,'0',127,
    0,0,0,
    0,0
};

static const int KB_BUFSIZE = 256;
static char kb_buf[KB_BUFSIZE];
static volatile int kb_head = 0, kb_tail = 0;
static bool shift_held  = false;
static bool caps_lock   = false;

static void kb_push(char c) {
    int next = (kb_head + 1) % KB_BUFSIZE;
    if (next != kb_tail) { kb_buf[kb_head] = c; kb_head = next; }
}

static void keyboard_irq(Registers*) {
    uint8_t sc = inb(0x60);
    bool released = sc & 0x80;
    sc &= 0x7F;

    if (sc == 0x2A || sc == 0x36)      { shift_held = !released; return; }
    if (sc == 0x3A && !released)       { caps_lock = !caps_lock; return; }
    if (released) return;

    char c = shift_held ? SCANCODE_SHIFT[sc] : SCANCODE_MAP[sc];
    if (caps_lock && c >= 'a' && c <= 'z') c -= 32;
    if (caps_lock && c >= 'A' && c <= 'Z' && shift_held) c += 32;
    if (c) kb_push(c);
}

void keyboard_init() {
    irq_install_handler(1, keyboard_irq);
}

int keyboard_available() {
    return kb_head != kb_tail;
}

char keyboard_peek() {
    if (!keyboard_available()) return 0;
    return kb_buf[kb_tail];
}

char keyboard_getchar() {
    while (!keyboard_available())
        __asm__ volatile("hlt");
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFSIZE;
    return c;
}

void keyboard_readline(char* buf, int max_len) {
    int pos = 0;
    while (pos < max_len - 1) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') break;
        if (c == '\b') {
            if (pos > 0) {
                pos--;
                vga.putChar('\b');
            }
            continue;
        }
        buf[pos++] = c;
        vga.putChar(c);
    }
    buf[pos] = '\0';
    vga.putChar('\n');
}
