#include "../include/gui_terminal.h"
#include "../include/gui_vi.h"
#include "../include/wm.h"
#include "../include/fb.h"
#include "../include/shell.h"
#include "../include/nodev.h"       // nodev_set_inputs, NODEV_MAX_INPUTS
#include "../include/vfs.h"         // vfs_resolve, vfs_read
#include "../include/kstring.h"
#include "../include/pit.h"
#include "../include/settings_config.h"

//  Terminal buffer
static const int CHAR_W     = 8;
static const int CHAR_H     = 14;
static const int MAX_COLS   = 120;
static const int MAX_ROWS   = 40;
static const int SCROLLBACK = 400;

struct TermRow {
    char     text[MAX_COLS + 1];
    uint32_t color[MAX_COLS];
};

static TermRow  s_buf[SCROLLBACK];
static int      s_buf_head = 0;
static int      s_buf_used = 0;
static int      s_scroll   = 0;

static char     s_input[512];
static int      s_input_len = 0;

static int      s_wid        = -1;
static bool     s_cursor_vis = true;
static bool     s_dirty      = true;

//  Shell output redirect 
static char s_pending[4096];
static int  s_pending_len = 0;

void gui_terminal_print(const char* s) {
    while (*s) {
        if (s_pending_len < (int)sizeof(s_pending) - 1)
            s_pending[s_pending_len++] = *s;
        s++;
    }
}


//  pin() input collection state 
enum TermState { TERM_NORMAL, TERM_COLLECTING_INPUT };

static TermState s_state        = TERM_NORMAL;
static char   s_pin_file   [VFS_MAX_PATH];
static char   s_pin_vars   [NODEV_MAX_INPUTS][32];
static char   s_pin_answers[NODEV_MAX_INPUTS][64];
static int    s_pin_total   = 0;
static int    s_pin_current = 0;

//  Scan source for pin(varname) calls 
static int scan_pin_calls(const char* src,
                           char varnames[][32], int max) {
    int count = 0;
    const char* p = src;
    while (*p && count < max) {
        if (k_strncmp(p, "pin(", 4) == 0) {
            p += 4;
            while (*p == ' ' || *p == '\t') p++;
            int vi = 0;
            while ( (*p >= 'a' && *p <= 'z') ||
                    (*p >= 'A' && *p <= 'Z') ||
                    (*p >= '0' && *p <= '9') ||
                     *p == '_' ) {
                if (vi < 31) varnames[count][vi++] = *p;
                p++;
            }
            varnames[count][vi] = '\0';
            count++;
        } else {
            p++;
        }
    }
    return count;
}

//  Append text to terminal buffer 
static void append_char(char c, uint32_t col) {
    if (c == '\b') {
        TermRow* row = &s_buf[s_buf_head];
        int len = (int)k_strlen(row->text);
        if (len > 0) row->text[len - 1] = '\0';
        s_dirty = true;
        return;
    }
    if (c == '\n') {
        s_buf_head = (s_buf_head + 1) % SCROLLBACK;
        s_buf[s_buf_head].text[0] = '\0';
        for (int i = 0; i < MAX_COLS; i++) s_buf[s_buf_head].color[i] = col;
        if (s_buf_used < SCROLLBACK) s_buf_used++;
        s_dirty = true;
        return;
    }
    TermRow* row = &s_buf[s_buf_head];
    int len = (int)k_strlen(row->text);
    if (len >= MAX_COLS) { append_char('\n', col); row = &s_buf[s_buf_head]; len = 0; }
    row->text[len]     = c;
    row->text[len + 1] = '\0';
    row->color[len]    = col;
    s_dirty = true;
}

static void append_str(const char* s, uint32_t col) {
    while (*s) append_char(*s++, col);
}

//  Flush pending shell output into terminal buffer 
void gui_terminal_flush() {
    if (s_pending_len == 0) return;
    s_pending[s_pending_len] = '\0';
    append_str(s_pending, g_settings.term_fg);
    s_pending_len = 0;
    s_dirty = true;
}

//  Prompt helpers 

// Returns a pointer to the last component of a VFS path — no alloc, no copy.
// "/home/user/projects" → "projects"
// "/home"               → "home"
// "/"                   → "/"
static const char* prompt_basename(const char* p) {
    const char* last = p;
    for (const char* s = p; *s; s++)
        if (*s == '/' && *(s + 1) != '\0') last = s + 1;
    return last;
}

static void draw_prompt() {
    char cwd[VFS_MAX_PATH];
    vfs_getcwd(cwd, VFS_MAX_PATH);

    append_str(g_settings.hostname,  0x44FF88);   // green  — hostname
    append_str("@",                  0x888888);   // grey   — separator
    append_str(prompt_basename(cwd), 0x44CCFF);   // cyan   — current dir
    append_str("> ",                 0xFFDD00);   // yellow — prompt char
}

static void draw_pin_prompt() {
    append_str("  pin(", 0xFF8800);
    append_str(s_pin_vars[s_pin_current], 0xFFCC44);
    append_str("): ", 0xFF8800);
    s_dirty = true;
}

//  Execute a normal shell command
static void exec_input() {
    s_input[s_input_len] = '\0';

    append_str(s_input, Color::TextNormal);
    append_char('\n', Color::TermFg);

    if (s_input_len == 0) {
        draw_prompt();
        s_dirty = true;
        return;
    }

    //  vi <filename> 
    bool is_vi = (k_strncmp(s_input, "vi ", 3) == 0 ||
                  k_strcmp (s_input, "vi")   == 0);
    if (is_vi) {
        const char* fname = (s_input_len > 3) ? s_input + 3 : nullptr;
        if (fname) while (*fname == ' ') fname++;
        gui_vi_open((fname && fname[0]) ? fname : "untitled");
        s_input_len = 0; s_input[0] = '\0';
        draw_prompt();
        s_dirty = true;
        return;
    }

    //  nodev <filename> 
    bool is_nodev = (k_strncmp(s_input, "nodev ", 6) == 0);
    if (is_nodev) {
        const char* fname = s_input + 6;
        while (*fname == ' ') fname++;

        if (!fname || fname[0] == '\0') {
            append_str("Usage: nodev <file.nod>\n", 0xFF4444);
            draw_prompt();
            s_input_len = 0; s_input[0] = '\0';
            s_dirty = true;
            return;
        }

        static char src[VFS_MAX_FILESIZE + 1];
        char resolved[VFS_MAX_PATH];
        vfs_resolve(fname, resolved, VFS_MAX_PATH);

        if (vfs_read(resolved, src, VFS_MAX_FILESIZE) < 0) {
            append_str("[NodeV] File not found: ", 0xFF4444);
            append_str(fname, 0xFF4444);
            append_char('\n', Color::TermFg);
            draw_prompt();
            s_input_len = 0; s_input[0] = '\0';
            s_dirty = true;
            return;
        }
        src[VFS_MAX_FILESIZE] = '\0';

        int pin_count = scan_pin_calls(src, s_pin_vars, NODEV_MAX_INPUTS);

        if (pin_count == 0) {
            append_str("[NodeV] Running: ", Color::Cyan);
            append_str(fname, Color::Cyan);
            append_char('\n', Color::TermFg);

            shell_set_gui_output(true);
            shell_exec(s_input);
            shell_set_gui_output(false);
            gui_terminal_flush();

            s_input_len = 0; s_input[0] = '\0';
            draw_prompt();
            s_dirty = true;
            return;
        }

        k_strncpy(s_pin_file, resolved, VFS_MAX_PATH - 1);
        s_pin_file[VFS_MAX_PATH - 1] = '\0';
        s_pin_total   = pin_count;
        s_pin_current = 0;
        for (int i = 0; i < NODEV_MAX_INPUTS; i++) s_pin_answers[i][0] = '\0';

        append_str("[NodeV] Running: ", Color::Cyan);
        append_str(fname, Color::Cyan);
        append_str(" (needs ", 0x8899AA);
        char nbuf[8]; k_itoa(pin_count, nbuf, 10);
        append_str(nbuf, 0x8899AA);
        append_str(" input(s) -- press Esc to cancel)\n", 0x8899AA);

        s_state = TERM_COLLECTING_INPUT;
        draw_pin_prompt();

        s_input_len = 0; s_input[0] = '\0';
        s_dirty = true;
        return;
    }

    //  Normal command 
    shell_set_gui_output(true);
    shell_exec(s_input);
    shell_set_gui_output(false);
    gui_terminal_flush();

    s_input_len = 0; s_input[0] = '\0';
    draw_prompt();
    s_dirty = true;
}

//  Handle Enter while collecting pin() inputs 
static void finish_pin_input() {
    s_input[s_input_len] = '\0';

    k_strncpy(s_pin_answers[s_pin_current], s_input, 63);
    s_pin_answers[s_pin_current][63] = '\0';
    append_str(s_input, Color::TextNormal);
    append_char('\n', Color::TermFg);

    s_input_len = 0; s_input[0] = '\0';
    s_pin_current++;

    if (s_pin_current < s_pin_total) {
        draw_pin_prompt();
    } else {
        s_state = TERM_NORMAL;

        typedef const char PinRow[64];
        nodev_set_inputs((PinRow*)s_pin_answers, s_pin_total);

        shell_set_gui_output(true);
        nodev_run_file(s_pin_file);
        shell_set_gui_output(false);
        gui_terminal_flush();

        draw_prompt();
    }
    s_dirty = true;
}

//  WM draw callback
void gui_terminal_draw(int, int cx, int cy, int cw, int ch, void*) {
    fb_fill_rect(cx, cy, cw, ch, g_settings.term_bg);

    int rows_visible = ch / CHAR_H;
    if (rows_visible > MAX_ROWS) rows_visible = MAX_ROWS;

    s_cursor_vis = (pit_uptime_s() % 2 == 0);

    int total      = s_buf_used;
    int first      = s_buf_head - total + 1;
    int view_start = total - rows_visible - s_scroll;
    if (view_start < 0) view_start = 0;

    for (int r = 0; r < rows_visible; r++) {
        int abs_idx = first + view_start + r;
        int buf_idx = ((abs_idx) % SCROLLBACK + SCROLLBACK) % SCROLLBACK;
        TermRow* row = &s_buf[buf_idx];
        int px = cx + 4;
        int py = cy + r * CHAR_H + 2;
        int len = (int)k_strlen(row->text);
        for (int col = 0; col < len && px + CHAR_W < cx + cw; col++) {
            fb_draw_char(px, py, row->text[col], row->color[col], g_settings.term_bg);
            px += CHAR_W;
        }
    }

    // Input: draw inline after prompt on the same screen row.
    uint32_t input_fg  = (s_state == TERM_COLLECTING_INPUT) ? 0xFFCC44 : Color::TextNormal;
    uint32_t cursor_fg = (s_state == TERM_COLLECTING_INPUT) ? 0xFFAA00 : g_settings.term_cursor;

    int prompt_screen_row = (total - 1) - view_start;
    int input_draw_y, input_x;
    if (prompt_screen_row >= 0 && prompt_screen_row < rows_visible) {
        input_draw_y = cy + prompt_screen_row * CHAR_H + 2;
        int prompt_len = (int)k_strlen(s_buf[s_buf_head].text);
        input_x = cx + 4 + prompt_len * CHAR_W;
    } else {
        input_draw_y = cy + ch - CHAR_H - 2;
        input_x = cx + 4;
    }

    for (int i = 0; i < s_input_len && input_x + CHAR_W < cx + cw; i++) {
        fb_draw_char(input_x, input_draw_y, s_input[i], input_fg, g_settings.term_bg);
        input_x += CHAR_W;
    }
    if (s_cursor_vis)
        fb_fill_rect(input_x, input_draw_y, CHAR_W - 1, CHAR_H - 2, cursor_fg);

    // Scrollbar
    if (s_buf_used > rows_visible) {
        int sb_x = cx + cw - 6;
        fb_fill_rect(sb_x, cy, 4, ch, Color::DarkGrey);
        int thumb_h = ch * rows_visible / s_buf_used;
        if (thumb_h < 16) thumb_h = 16;
        int thumb_y = cy + (ch - thumb_h) * (s_buf_used - rows_visible - s_scroll)
                          / (s_buf_used - rows_visible + 1);
        fb_fill_rect(sb_x, thumb_y, 4, thumb_h, Color::MidGrey);
    }
}

//  WM key callback
void gui_terminal_key(int, char key, void*) {
    if (key == '\n' || key == '\r') {
        if (s_state == TERM_COLLECTING_INPUT)
            finish_pin_input();
        else
            exec_input();
        return;
    }
    if (key == '\b') {
        if (s_input_len > 0) s_input_len--;
        s_dirty = true;
        return;
    }
    if (key == 27) {   // ESC
        if (s_state == TERM_COLLECTING_INPUT) {
            s_state = TERM_NORMAL;
            append_str("\n[NodeV] Input cancelled.\n", 0xFF4444);
            draw_prompt();
        }
        s_input_len = 0;
        s_dirty = true;
        return;
    }

    if (s_input_len < (int)sizeof(s_input) - 2) {
        s_input[s_input_len++] = key;
        s_dirty = true;
    }
}

//  WM close callback
void gui_terminal_close(int wid, void*) {
    wm_destroy(wid);
    s_wid = -1;
}

//  Open (or re-focus)
//  Clear  — resets buffer so prompt snaps back to row 0
void gui_terminal_clear() {
    for (int i = 0; i < SCROLLBACK; i++) {
        s_buf[i].text[0] = '\0';
        for (int j = 0; j < MAX_COLS; j++) s_buf[i].color[j] = Color::TermFg;
    }
    s_buf_head  = 0;
    s_buf_used  = 1;
    s_scroll    = 0;
    s_input_len = 0;
    s_input[0]  = '\0';
    s_pending_len = 0;
    // Do NOT call draw_prompt() here — exec_input() always does it after
    // shell_exec() returns, which would produce a double prompt.
    s_dirty = true;
}

void gui_terminal_open() {
    if (s_wid >= 0) {
        wm_show(s_wid);
        wm_focus(s_wid);
        return;
    }

    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        for (int i = 0; i < SCROLLBACK; i++) {
            s_buf[i].text[0] = '\0';
            for (int j = 0; j < MAX_COLS; j++) s_buf[i].color[j] = Color::TermFg;
        }
        s_buf_head = 0;
        s_buf_used = 1;
        draw_prompt();
    }

    s_wid = wm_create("Terminal", 60, 50, 820, 560);
    wm_set_callbacks(s_wid,
                     gui_terminal_draw,
                     gui_terminal_key,
                     gui_terminal_close,
                     nullptr, nullptr);
}