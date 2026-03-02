#pragma once

// Kernel shell interface

void shell_init();                     // init shell
void shell_run();                      // start interactive loop
void shell_exec(const char* line);     // execute command line

void shell_set_gui_output(bool active); // toggle GUI output redirect
bool shell_gui_output_active();        // GUI mode active?
void shell_print(const char* str);     // print text
void shell_println(const char* str);   // print line