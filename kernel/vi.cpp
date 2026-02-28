#include "../include/vi.h"
#include "../include/vga.h"
#include "../include/vfs.h"
#include "../include/keyboard.h"
#include "../include/kstring.h"


//  Constants
static const int VI_COLS      = 80;
static const int VI_ROWS      = 24;   // row 24 is the status/command bar
static const int VI_MAX_LINES = 256;
static const int VI_LINE_LEN  = 256;  // max chars per line (including \0)

// Special key codes returned by keyboard_getchar()
// (must match what keyboard.cpp emits for arrow keys)
static const char KEY_UP    = (char)0xE0 - 3;   // 0xDD  — see keyboard.cpp
static const char KEY_DOWN  = (char)0xE0 - 2;   // 0xDE
static const char KEY_LEFT  = (char)0xE0 - 1;   // 0xDF
static const char KEY_RIGHT = (char)0xE0;        // 0xE0
static const char KEY_ESC   = 0x1B;
static const char KEY_BS    = 0x08;
static const char KEY_DEL   = 0x7F;
static const char KEY_ENTER = '\n';


//  Editor state

enum ViMode { MODE_NORMAL, MODE_INSERT, MODE_COMMAND };

struct ViState {
    char  lines[VI_MAX_LINES][VI_LINE_LEN];
    int   line_len[VI_MAX_LINES];   // actual length (no \0)
    int   num_lines;                // total lines in buffer

    int   cx, cy;                   // cursor: col, row  (into buffer)
    int   scroll_row;               // first visible buffer row

    ViMode mode;
    char   cmd_buf[80];             // command-line buffer  (:w, :q, …)
    int    cmd_len;

    char   filename[VFS_MAX_PATH];
    bool   dirty;                   // unsaved changes
    char   status_msg[80];          // shown in status bar
};

static ViState vi;


//  Low-level helpers

static void vi_clear_screen() {
    for (int r = 0; r < 25; r++)
        for (int c = 0; c < VI_COLS; c++)
            vga.write_cell(r, c, ' ', LIGHT_GREY, BLACK);
}

// Draw one screen row (r = screen row 0..VI_ROWS-1)
static void vi_draw_line(int screen_row) {
    int buf_row = vi.scroll_row + screen_row;
    int col = 0;

    if (buf_row < vi.num_lines) {
        int len = vi.line_len[buf_row];
        for (; col < len && col < VI_COLS; col++) {
            char ch = vi.lines[buf_row][col];
            vga.write_cell(screen_row, col, ch, WHITE, BLACK);
        }
    }
    // Tilde for lines past end of file (like real vi)
    if (buf_row >= vi.num_lines) {
        vga.write_cell(screen_row, 0, '~', DARK_GREY, BLACK);
        col = 1;
    }
    // Pad rest of line with spaces
    for (; col < VI_COLS; col++)
        vga.write_cell(screen_row, col, ' ', LIGHT_GREY, BLACK);
}

// Draw the status bar (row 24)
static void vi_draw_status() {
    // Choose colours based on mode
    VGAColor bg = DARK_GREY, fg = WHITE;
    if (vi.mode == MODE_INSERT)  { bg = BLUE;  fg = WHITE; }
    if (vi.mode == MODE_COMMAND) { bg = BLACK; fg = YELLOW; }

    // Clear status row
    for (int c = 0; c < VI_COLS; c++)
        vga.write_cell(24, c, ' ', fg, bg);

    if (vi.mode == MODE_COMMAND) {
        // Show ":" + command buffer
        vga.write_cell(24, 0, ':', YELLOW, BLACK);
        for (int i = 0; i < vi.cmd_len && i + 1 < VI_COLS; i++)
            vga.write_cell(24, i + 1, vi.cmd_buf[i], YELLOW, BLACK);
        return;
    }

    // Left side: mode name + filename + dirty flag
    char left[60]; left[0] = '\0';
    if (vi.mode == MODE_INSERT) k_strcat(left, "-- INSERT -- ");
    else                        k_strcat(left, "-- NORMAL -- ");
    k_strcat(left, vi.filename[0] ? vi.filename : "[No Name]");
    if (vi.dirty) k_strcat(left, " [+]");

    // Right side: cursor position
    char pos[20];
    // Build "row,col" manually
    char tmp[12];
    k_itoa(vi.cy + 1, tmp, 10); k_strcpy(pos, tmp);
    k_strcat(pos, ",");
    k_itoa(vi.cx + 1, tmp, 10); k_strcat(pos, tmp);

    // Write left
    int col = 0;
    for (; left[col] && col < VI_COLS; col++)
        vga.write_cell(24, col, left[col], fg, bg);

    // Write status message (centre/right)
    if (vi.status_msg[0]) {
        int msglen = k_strlen(vi.status_msg);
        int start = VI_COLS - msglen - k_strlen(pos) - 2;
        if (start < col + 1) start = col + 1;
        for (int i = 0; vi.status_msg[i] && start + i < VI_COLS - (int)k_strlen(pos) - 1; i++)
            vga.write_cell(24, start + i, vi.status_msg[i], YELLOW, bg);
    }

    // Write position at far right
    int plen = k_strlen(pos);
    int pstart = VI_COLS - plen;
    for (int i = 0; i < plen; i++)
        vga.write_cell(24, pstart + i, pos[i], LIGHT_CYAN, bg);
}

// Full redraw
static void vi_redraw() {
    for (int r = 0; r < VI_ROWS; r++)
        vi_draw_line(r);
    vi_draw_status();

    // Place the hardware cursor
    int screen_row = vi.cy - vi.scroll_row;
    vga.set_cursor(screen_row, vi.cx);
}

// Redraw only the current line + status bar (fast path for inserts)
static void vi_redraw_line() {
    int sr = vi.cy - vi.scroll_row;
    if (sr >= 0 && sr < VI_ROWS) vi_draw_line(sr);
    vi_draw_status();
    vga.set_cursor(sr, vi.cx);
}


//  Buffer operations

// Clamp cursor to valid position
static void vi_clamp() {
    if (vi.cy < 0) vi.cy = 0;
    if (vi.cy >= vi.num_lines) vi.cy = vi.num_lines - 1;
    if (vi.cy < 0) vi.cy = 0;

    int max_cx = vi.line_len[vi.cy];
    if (vi.mode != MODE_INSERT && max_cx > 0) max_cx--;   // NORMAL: can't go past last char
    if (vi.cx > max_cx) vi.cx = max_cx;
    if (vi.cx < 0) vi.cx = 0;

    // Scroll
    if (vi.cy < vi.scroll_row) vi.scroll_row = vi.cy;
    if (vi.cy >= vi.scroll_row + VI_ROWS) vi.scroll_row = vi.cy - VI_ROWS + 1;
}

// Insert character at (cx, cy)
static void vi_insert_char(char c) {
    if (vi.line_len[vi.cy] >= VI_LINE_LEN - 1) return;
    char* line = vi.lines[vi.cy];
    int len = vi.line_len[vi.cy];
    // Shift right
    for (int i = len; i > vi.cx; i--) line[i] = line[i - 1];
    line[vi.cx] = c;
    vi.line_len[vi.cy]++;
    line[vi.line_len[vi.cy]] = '\0';
    vi.cx++;
    vi.dirty = true;
}

// Split line at cx (Enter in insert mode)
static void vi_split_line() {
    if (vi.num_lines >= VI_MAX_LINES) return;
    // Push lines below down
    for (int i = vi.num_lines; i > vi.cy + 1; i--) {
        k_memcpy(vi.lines[i], vi.lines[i-1], VI_LINE_LEN);
        vi.line_len[i] = vi.line_len[i-1];
    }
    // Copy tail of current line to new line
    char* cur  = vi.lines[vi.cy];
    char* next = vi.lines[vi.cy + 1];
    int tail = vi.line_len[vi.cy] - vi.cx;
    if (tail > 0) {
        k_memcpy(next, cur + vi.cx, tail);
        next[tail] = '\0';
        vi.line_len[vi.cy + 1] = tail;
    } else {
        next[0] = '\0';
        vi.line_len[vi.cy + 1] = 0;
    }
    // Truncate current line
    cur[vi.cx] = '\0';
    vi.line_len[vi.cy] = vi.cx;
    vi.num_lines++;
    vi.cy++;
    vi.cx = 0;
    vi.dirty = true;
}

// Backspace in insert mode
static void vi_backspace() {
    if (vi.cx > 0) {
        char* line = vi.lines[vi.cy];
        int len = vi.line_len[vi.cy];
        for (int i = vi.cx - 1; i < len - 1; i++) line[i] = line[i+1];
        line[len-1] = '\0';
        vi.line_len[vi.cy]--;
        vi.cx--;
        vi.dirty = true;
    } else if (vi.cy > 0) {
        // Join with previous line
        int prev_len = vi.line_len[vi.cy - 1];
        int cur_len  = vi.line_len[vi.cy];
        if (prev_len + cur_len < VI_LINE_LEN - 1) {
            char* prev = vi.lines[vi.cy - 1];
            char* cur  = vi.lines[vi.cy];
            k_memcpy(prev + prev_len, cur, cur_len);
            vi.line_len[vi.cy - 1] = prev_len + cur_len;
            prev[prev_len + cur_len] = '\0';
            // Remove current line
            for (int i = vi.cy; i < vi.num_lines - 1; i++) {
                k_memcpy(vi.lines[i], vi.lines[i+1], VI_LINE_LEN);
                vi.line_len[i] = vi.line_len[i+1];
            }
            vi.num_lines--;
            vi.cy--;
            vi.cx = prev_len;
            vi.dirty = true;
        }
    }
}

// Delete char at cx (x in NORMAL mode)
static void vi_delete_char() {
    if (vi.num_lines == 0) return;
    int len = vi.line_len[vi.cy];
    if (len == 0) return;
    char* line = vi.lines[vi.cy];
    for (int i = vi.cx; i < len - 1; i++) line[i] = line[i+1];
    line[len-1] = '\0';
    vi.line_len[vi.cy]--;
    vi.dirty = true;
    vi_clamp();
}

// Delete entire line (dd)
static void vi_delete_line() {
    if (vi.num_lines <= 1) {
        vi.lines[0][0] = '\0'; vi.line_len[0] = 0;
        vi.cx = 0; vi.dirty = true; return;
    }
    for (int i = vi.cy; i < vi.num_lines - 1; i++) {
        k_memcpy(vi.lines[i], vi.lines[i+1], VI_LINE_LEN);
        vi.line_len[i] = vi.line_len[i+1];
    }
    vi.num_lines--;
    vi.dirty = true;
    vi_clamp();
}

// Open a new blank line below (o) and enter insert mode
static void vi_open_line_below() {
    if (vi.num_lines >= VI_MAX_LINES) return;
    for (int i = vi.num_lines; i > vi.cy + 1; i--) {
        k_memcpy(vi.lines[i], vi.lines[i-1], VI_LINE_LEN);
        vi.line_len[i] = vi.line_len[i-1];
    }
    vi.cy++;
    vi.lines[vi.cy][0] = '\0';
    vi.line_len[vi.cy] = 0;
    vi.num_lines++;
    vi.cx = 0;
    vi.mode = MODE_INSERT;
    vi.dirty = true;
}

// Open a new blank line above (O)
static void vi_open_line_above() {
    if (vi.num_lines >= VI_MAX_LINES) return;
    for (int i = vi.num_lines; i > vi.cy; i--) {
        k_memcpy(vi.lines[i], vi.lines[i-1], VI_LINE_LEN);
        vi.line_len[i] = vi.line_len[i-1];
    }
    vi.lines[vi.cy][0] = '\0';
    vi.line_len[vi.cy] = 0;
    vi.num_lines++;
    vi.cx = 0;
    vi.mode = MODE_INSERT;
    vi.dirty = true;
}


//  File I/O

// Load file content into line buffer
static void vi_load_file() {
    static char raw[VFS_MAX_FILESIZE];
    vi.num_lines = 0;
    vi.line_len[0] = 0; vi.lines[0][0] = '\0';

    int n = vfs_read(vi.filename, raw, VFS_MAX_FILESIZE);
    if (n < 0) {
        // New file — start with one empty line
        vi.num_lines = 1; vi.line_len[0] = 0; vi.lines[0][0] = '\0';
        k_strcpy(vi.status_msg, "New file");
        return;
    }

    int row = 0, col = 0;
    vi.lines[row][0] = '\0'; vi.line_len[row] = 0;
    for (int i = 0; i < n && row < VI_MAX_LINES; i++) {
        char c = raw[i];
        if (c == '\n' || c == '\r') {
            vi.lines[row][col] = '\0';
            vi.line_len[row] = col;
            row++; col = 0;
            vi.lines[row][0] = '\0'; vi.line_len[row] = 0;
        } else if (col < VI_LINE_LEN - 1) {
            vi.lines[row][col++] = c;
        }
    }
    vi.lines[row][col] = '\0';
    vi.line_len[row] = col;
    vi.num_lines = row + 1;
    if (vi.num_lines == 0) vi.num_lines = 1;
}

// Flatten line buffer back to a single string and write to VFS
static int vi_save_file(const char* fname) {
    static char raw[VFS_MAX_FILESIZE];
    int pos = 0;
    for (int r = 0; r < vi.num_lines && pos < (int)VFS_MAX_FILESIZE - 2; r++) {
        int len = vi.line_len[r];
        if (pos + len >= (int)VFS_MAX_FILESIZE - 2) len = VFS_MAX_FILESIZE - 2 - pos;
        k_memcpy(raw + pos, vi.lines[r], len);
        pos += len;
        raw[pos++] = '\n';
    }
    raw[pos] = '\0';
    int written = vfs_write(fname, raw, pos);
    return written >= 0 ? 0 : -1;
}


//  Command-line processing  (:w, :q, :wq, …)

// Returns true if editor should exit
static bool vi_exec_command() {
    char* cmd = vi.cmd_buf;

    // :w [filename]
    if (cmd[0] == 'w') {
        char saveas[VFS_MAX_PATH];
        if (cmd[1] == ' ' && cmd[2]) {
            k_strncpy(saveas, cmd + 2, VFS_MAX_PATH - 1);
        } else {
            k_strncpy(saveas, vi.filename, VFS_MAX_PATH - 1);
        }
        if (!saveas[0]) {
            k_strcpy(vi.status_msg, "No filename!");
            return false;
        }
        if (vi_save_file(saveas) == 0) {
            k_strncpy(vi.filename, saveas, VFS_MAX_PATH - 1);
            vi.dirty = false;
            k_strcpy(vi.status_msg, "File saved");
        } else {
            k_strcpy(vi.status_msg, "Write failed!");
        }
        return false;
    }

    // :q  or  :q!
    if (cmd[0] == 'q') {
        if (cmd[1] == '!') return true;   // force quit
        if (vi.dirty) {
            k_strcpy(vi.status_msg, "Unsaved changes! Use :q! to force quit.");
            return false;
        }
        return true;
    }

    // :wq  or  :x
    if ((cmd[0]=='w' && cmd[1]=='q') || cmd[0]=='x') {
        char saveas[VFS_MAX_PATH];
        k_strncpy(saveas, vi.filename, VFS_MAX_PATH - 1);
        if (!saveas[0]) {
            k_strcpy(vi.status_msg, "No filename! Use :w <name>");
            return false;
        }
        vi_save_file(saveas);
        vi.dirty = false;
        return true;
    }

    // :N  — go to line N
    if (cmd[0] >= '0' && cmd[0] <= '9') {
        int n = k_atoi(cmd) - 1;
        if (n < 0) n = 0;
        if (n >= vi.num_lines) n = vi.num_lines - 1;
        vi.cy = n; vi.cx = 0;
        vi_clamp();
        return false;
    }

    k_strcpy(vi.status_msg, "Unknown command");
    return false;
}


//  Keystroke handlers per mode

static bool vi_handle_normal(char c) {
    vi.status_msg[0] = '\0';

    // Movement
    if (c == 'h' || c == KEY_LEFT)  { vi.cx--; vi_clamp(); vi_draw_status(); vga.set_cursor(vi.cy-vi.scroll_row, vi.cx); return false; }
    if (c == 'l' || c == KEY_RIGHT) { vi.cx++; vi_clamp(); vi_draw_status(); vga.set_cursor(vi.cy-vi.scroll_row, vi.cx); return false; }
    if (c == 'k' || c == KEY_UP) {
        vi.cy--; vi_clamp();
        if (vi.cy - vi.scroll_row < 0) vi_redraw(); else { vi_draw_status(); vga.set_cursor(vi.cy-vi.scroll_row, vi.cx); }
        return false;
    }
    if (c == 'j' || c == KEY_DOWN) {
        vi.cy++; vi_clamp();
        if (vi.cy - vi.scroll_row >= VI_ROWS) vi_redraw(); else { vi_draw_status(); vga.set_cursor(vi.cy-vi.scroll_row, vi.cx); }
        return false;
    }

    // Jump to start / end of line
    if (c == '0') { vi.cx = 0; vi_draw_status(); vga.set_cursor(vi.cy-vi.scroll_row, vi.cx); return false; }
    if (c == '$') { vi.cx = vi.line_len[vi.cy]; if (vi.cx > 0) vi.cx--; vi_draw_status(); vga.set_cursor(vi.cy-vi.scroll_row, vi.cx); return false; }

    // Jump to first / last line
    if (c == 'G') { vi.cy = vi.num_lines - 1; vi.cx = 0; vi_clamp(); vi_redraw(); return false; }

    // Enter insert mode
    if (c == 'i') { vi.mode = MODE_INSERT; vi_draw_status(); return false; }

    // 'a' — insert after cursor
    if (c == 'a') {
        if (vi.line_len[vi.cy] > 0) vi.cx++;
        vi.mode = MODE_INSERT;
        vi_clamp(); vi_draw_status();
        return false;
    }

    // 'A' — insert at end of line
    if (c == 'A') {
        vi.cx = vi.line_len[vi.cy];
        vi.mode = MODE_INSERT;
        vi_draw_status(); vga.set_cursor(vi.cy-vi.scroll_row, vi.cx);
        return false;
    }

    // 'I' — insert at start of line
    if (c == 'I') {
        vi.cx = 0; vi.mode = MODE_INSERT;
        vi_draw_status(); vga.set_cursor(vi.cy-vi.scroll_row, vi.cx);
        return false;
    }

    // 'o' / 'O' — open line below / above
    if (c == 'o') { vi_open_line_below(); vi_redraw(); return false; }
    if (c == 'O') { vi_open_line_above(); vi_redraw(); return false; }

    // 'x' — delete char under cursor
    if (c == 'x') { vi_delete_char(); vi_redraw_line(); return false; }

    // 'd' — must be followed by another 'd' for dd
    // We handle dd via a simple two-keystroke state using a static flag
    static bool pending_d = false;
    if (c == 'd') {
        if (pending_d) { vi_delete_line(); pending_d = false; vi_redraw(); }
        else pending_d = true;
        return false;
    }
    pending_d = false;   // any other key cancels pending d

    // 'g' — must be followed by 'g' for gg (first line)
    static bool pending_g = false;
    if (c == 'g') {
        if (pending_g) { vi.cy = 0; vi.cx = 0; pending_g = false; vi_clamp(); vi_redraw(); }
        else pending_g = true;
        return false;
    }
    pending_g = false;

    // Enter command mode
    if (c == ':') {
        vi.mode = MODE_COMMAND;
        vi.cmd_buf[0] = '\0'; vi.cmd_len = 0;
        vi_draw_status();
        return false;
    }

    return false;
}

static bool vi_handle_insert(char c) {
    if (c == KEY_ESC) {
        vi.mode = MODE_NORMAL;
        if (vi.cx > 0) vi.cx--;   // vi convention: back one on Esc
        vi_clamp(); vi_draw_status();
        vga.set_cursor(vi.cy - vi.scroll_row, vi.cx);
        return false;
    }
    if (c == KEY_ENTER) {
        vi_split_line();
        vi_clamp();
        if (vi.cy - vi.scroll_row >= VI_ROWS) vi_redraw();
        else vi_redraw();   // always full redraw on split (lines shift)
        return false;
    }
    if (c == KEY_BS || c == KEY_DEL) {
        vi_backspace();
        vi_clamp();
        vi_redraw();
        return false;
    }
    // Arrow keys work in insert mode too
    if (c == KEY_UP)    { vi.cy--; vi_clamp(); vi_redraw(); return false; }
    if (c == KEY_DOWN)  { vi.cy++; vi_clamp(); vi_redraw(); return false; }
    if (c == KEY_LEFT)  { vi.cx--; vi_clamp(); vi_draw_status(); vga.set_cursor(vi.cy-vi.scroll_row, vi.cx); return false; }
    if (c == KEY_RIGHT) { vi.cx++; vi_clamp(); vi_draw_status(); vga.set_cursor(vi.cy-vi.scroll_row, vi.cx); return false; }

    // Printable character
    if (c >= 0x20 && c < 0x7F) {
        vi_insert_char(c);
        vi_clamp();
        vi_redraw_line();
        return false;
    }
    return false;
}

static bool vi_handle_command(char c) {
    if (c == KEY_ESC) {
        vi.mode = MODE_NORMAL;
        vi.cmd_buf[0] = '\0'; vi.cmd_len = 0;
        vi.status_msg[0] = '\0';
        vi_draw_status();
        vga.set_cursor(vi.cy - vi.scroll_row, vi.cx);
        return false;
    }
    if (c == KEY_ENTER) {
        vi.cmd_buf[vi.cmd_len] = '\0';
        bool quit = vi_exec_command();
        vi.mode = MODE_NORMAL;
        vi.cmd_buf[0] = '\0'; vi.cmd_len = 0;
        vi_draw_status();
        return quit;
    }
    if ((c == KEY_BS || c == KEY_DEL) && vi.cmd_len > 0) {
        vi.cmd_buf[--vi.cmd_len] = '\0';
        vi_draw_status();
        return false;
    }
    if (c >= 0x20 && c < 0x7F && vi.cmd_len < 78) {
        vi.cmd_buf[vi.cmd_len++] = c;
        vi.cmd_buf[vi.cmd_len]   = '\0';
        vi_draw_status();
        return false;
    }
    return false;
}


//  Public entry point

int vi_open(const char* filename) {
    // Initialise state
    k_memset(&vi, 0, sizeof(vi));
    if (filename && filename[0])
        k_strncpy(vi.filename, filename, VFS_MAX_PATH - 1);
    vi.mode = MODE_NORMAL;
    vi.cx = vi.cy = vi.scroll_row = 0;
    vi.dirty = false;
    vi.status_msg[0] = '\0';

    // Load file (or start blank)
    vi_load_file();

    // Initial full render
    vi_clear_screen();
    vi_redraw();

    // Main event loop
    for (;;) {
        char c = keyboard_getchar();
        bool quit = false;
        switch (vi.mode) {
            case MODE_NORMAL:  quit = vi_handle_normal(c);  break;
            case MODE_INSERT:  quit = vi_handle_insert(c);  break;
            case MODE_COMMAND: quit = vi_handle_command(c); break;
        }
        if (quit) break;
    }

    // Restore shell appearance
    vga.init();
    return 0;
}