#pragma once
#include <stdint.h>

// PS/2 keyboard driver API
void keyboard_init();
char keyboard_getchar();        // blocking read
int  keyboard_available();      // non-blocking check: chars in buffer
char keyboard_peek();           // peek without consuming
void keyboard_readline(char* buf, int max_len);  // read until Enter
