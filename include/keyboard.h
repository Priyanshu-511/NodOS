#pragma once
#include <stdint.h>

void keyboard_init();
char keyboard_getchar();        // blocking read
int  keyboard_available();      // non-blocking check: chars in buffer
char keyboard_peek();           // peek without consuming
void keyboard_readline(char* buf, int max_len);  // read until Enter
