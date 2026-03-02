#include "../include/gui_vi.h"
#include "../include/wm.h"
#include "../include/fb.h"
#include "../include/vfs.h"
#include "../include/kstring.h"
#include "../include/heap.h"
#include "../include/pit.h"

//  Config
static const int GVI_CHAR_W    = 8;
static const int GVI_CHAR_H    = 14;
static const int GVI_MAX_LINES = 512;
static const int GVI_MAX_COLS  = 256;
static const int GVI_STATUS_H  = 16;   // pixels for status bar
static const int GVI_LN_CHARS  = 4;    // "999 " → 4 chars of line-number gutter

//  State  (supports a single editor instance; extend to array for multi-file)
enum GViMode { GVI_NORMAL, GVI_INSERT, GVI_COMMAND };

static int    s_wid       = -1;
static char   s_filename [VFS_MAX_PATH];
static char   s_lines    [GVI_MAX_LINES][GVI_MAX_COLS];
static int    s_nlines    = 1;
static int    s_cur_row   = 0;
static int    s_cur_col   = 0;
static int    s_scroll    = 0;      // index of first visible line
static GViMode s_mode     = GVI_NORMAL;
static char   s_cmd      [64];
static int    s_cmd_len   = 0;
static bool   s_modified  = false;
static char   s_status   [160];    // message shown in status bar
static char   s_last_key  = 0;     // for two-key commands (dd, gg)

//  Helpers
static void clamp_col() {
    int len = (int)k_strlen(s_lines[s_cur_row]);
    if (s_mode == GVI_NORMAL && len > 0 && s_cur_col >= len)
        s_cur_col = len - 1;
    if (s_cur_col < 0) s_cur_col = 0;
}

static void clamp_row() {
    if (s_cur_row < 0)        s_cur_row = 0;
    if (s_cur_row >= s_nlines) s_cur_row = s_nlines - 1;
}

static void scroll_to_cursor(int rows_vis) {
    if (s_cur_row < s_scroll)
        s_scroll = s_cur_row;
    if (s_cur_row >= s_scroll + rows_vis)
        s_scroll = s_cur_row - rows_vis + 1;
    if (s_scroll < 0) s_scroll = 0;
}

//  File I/O
static void load_file() {
    for (int i = 0; i < GVI_MAX_LINES; i++) s_lines[i][0] = '\0';
    s_nlines = 1;

    if (vfs_exists(s_filename) < 0) return;   // new file

    static char buf[VFS_MAX_FILESIZE + 1];
    buf[0] = '\0';
    if (vfs_read(s_filename, buf, VFS_MAX_FILESIZE) < 0) return;
    buf[VFS_MAX_FILESIZE] = '\0';

    int li = 0, ci = 0;
    for (int i = 0; buf[i] && li < GVI_MAX_LINES - 1; i++) {
        if (buf[i] == '\n') {
            s_lines[li][ci] = '\0';
            li++; ci = 0;
            s_lines[li][0] = '\0';
        } else if (ci < GVI_MAX_COLS - 1) {
            s_lines[li][ci++] = buf[i];
        }
    }
    s_lines[li][ci] = '\0';
    s_nlines = li + 1;
    if (s_nlines < 1) s_nlines = 1;
}

static bool save_file(const char* fname) {
    static char buf[VFS_MAX_FILESIZE + 2];
    int pos = 0;
    for (int i = 0; i < s_nlines && pos < (int)VFS_MAX_FILESIZE - 2; i++) {
        int len = (int)k_strlen(s_lines[i]);
        for (int j = 0; j < len && pos < (int)VFS_MAX_FILESIZE - 2; j++)
            buf[pos++] = s_lines[i][j];
        if (i < s_nlines - 1) buf[pos++] = '\n';
    }
    buf[pos] = '\0';

    if (vfs_exists(fname) < 0) vfs_create(fname);
    return vfs_write(fname, buf, (uint32_t)pos) >= 0;
}

//  Text editing primitives
static void insert_char_at_cursor(char c) {
    int len = (int)k_strlen(s_lines[s_cur_row]);
    if (len >= GVI_MAX_COLS - 1) return;
    for (int i = len; i >= s_cur_col; i--)
        s_lines[s_cur_row][i + 1] = s_lines[s_cur_row][i];
    s_lines[s_cur_row][s_cur_col++] = c;
    s_modified = true;
}

static void delete_char_at(int row, int col) {
    int len = (int)k_strlen(s_lines[row]);
    if (col < 0 || col >= len) return;
    for (int i = col; i < len; i++)
        s_lines[row][i] = s_lines[row][i + 1];
    s_modified = true;
}

static void split_line_at_cursor() {
    // Insert newline: split current line at cursor, push new line below
    if (s_nlines >= GVI_MAX_LINES - 1) return;
    for (int i = s_nlines; i > s_cur_row + 1; i--)
        k_strcpy(s_lines[i], s_lines[i - 1]);
    s_nlines++;
    k_strncpy(s_lines[s_cur_row + 1],
              s_lines[s_cur_row] + s_cur_col,
              GVI_MAX_COLS - 1);
    s_lines[s_cur_row + 1][GVI_MAX_COLS - 1] = '\0';
    s_lines[s_cur_row][s_cur_col] = '\0';
    s_cur_row++;
    s_cur_col = 0;
    s_modified = true;
}

static void delete_current_line() {
    if (s_nlines <= 1) { s_lines[0][0] = '\0'; s_modified = true; return; }
    for (int i = s_cur_row; i < s_nlines - 1; i++)
        k_strcpy(s_lines[i], s_lines[i + 1]);
    s_nlines--;
    clamp_row();
    clamp_col();
    s_modified = true;
}

static void merge_with_prev_line() {
    // Used when backspace at column 0 in INSERT mode
    if (s_cur_row == 0) return;
    int prev_len = (int)k_strlen(s_lines[s_cur_row - 1]);
    // Append current line to previous (if it fits)
    int cur_len  = (int)k_strlen(s_lines[s_cur_row]);
    if (prev_len + cur_len < GVI_MAX_COLS - 1)
        k_strcat(s_lines[s_cur_row - 1], s_lines[s_cur_row]);
    else
        s_lines[s_cur_row - 1][GVI_MAX_COLS - 1] = '\0';
    // Remove current line
    for (int i = s_cur_row; i < s_nlines - 1; i++)
        k_strcpy(s_lines[i], s_lines[i + 1]);
    s_nlines--;
    s_cur_row--;
    s_cur_col = prev_len;
    s_modified = true;
}


//  Command handler  (:w :q :q! :wq :x :w <file>)

static void handle_command() {
    s_cmd[s_cmd_len] = '\0';

    bool do_write = false;
    bool do_quit  = false;

    if (k_strcmp(s_cmd, "q") == 0) {
        if (s_modified)
            k_strcpy(s_status, "Unsaved changes! Use  :q!  to discard,  :wq  to save.");
        else
            do_quit = true;

    } else if (k_strcmp(s_cmd, "q!") == 0) {
        do_quit = true;

    } else if (k_strcmp(s_cmd, "w") == 0) {
        do_write = true;

    } else if (k_strncmp(s_cmd, "w ", 2) == 0 && s_cmd_len > 2) {
        // :w <filename>
        k_strncpy(s_filename, s_cmd + 2, VFS_MAX_PATH - 1);
        s_filename[VFS_MAX_PATH - 1] = '\0';
        do_write = true;

    } else if (k_strcmp(s_cmd, "wq") == 0 || k_strcmp(s_cmd, "x") == 0) {
        do_write = true;
        do_quit  = true;

    } else {
        k_strcpy(s_status, "Unknown command. Try :w  :q  :wq  :q!");
    }

    if (do_write) {
        if (save_file(s_filename)) {
            s_modified = false;
            k_strcpy(s_status, "Written.");
        } else {
            k_strcpy(s_status, "Write failed!");
            do_quit = false;   // don't close on failed save
        }
    }

    if (do_quit) {
        wm_destroy(s_wid);
        s_wid = -1;
    }
}


//  WM draw callback

static void vi_draw(int, int cx, int cy, int cw, int ch, void*) {
    // 1. THIS IS THE FIX: Push the entire drawing area down by 24 pixels
    int top_pad = 2;
    cy += top_pad;
    ch -= top_pad;

    // 2. THIS CONTROLS LEFT/RIGHT SHIFT (Increase to shift right, decrease to shift left)
    int left_pad = 0; 

    if (cw <= 0 || ch <= GVI_STATUS_H) return;

    int text_h   = ch - GVI_STATUS_H;
    int rows_vis = text_h / GVI_CHAR_H;
    int ln_w     = GVI_LN_CHARS * GVI_CHAR_W;
    
    // 3. APPLY PADDING SAFELY
    int text_x   = cx + ln_w + left_pad;
    int text_w   = cw - ln_w - left_pad; // Shrink total width to prevent crash

    scroll_to_cursor(rows_vis);

    //  Clear 
    fb_fill_rect(cx, cy, cw, text_h, 0x080D16);

    //  Lines 
    for (int r = 0; r < rows_vis; r++) {
        int li = s_scroll + r;
        int py = cy + r * GVI_CHAR_H;

        if (li >= s_nlines) {
            fb_draw_char(cx + GVI_CHAR_W / 2, py, '~', 0x2A4060, 0x080D16);
            continue;
        }

        bool is_cur = (li == s_cur_row);
        uint32_t row_bg = is_cur ? 0x0C1A2C : 0x080D16;

        if (is_cur) fb_fill_rect(text_x, py, text_w, GVI_CHAR_H, row_bg);

        char lnbuf[8];
        k_itoa(li + 1, lnbuf, 10);
        int lnlen = (int)k_strlen(lnbuf);
        int lnx   = cx + GVI_CHAR_W / 2;
        fb_draw_text(lnx, py, lnbuf, is_cur ? 0xAAFF88 : 0x3A7A4A, 0x080D16);

        int len = (int)k_strlen(s_lines[li]);
        for (int c = 0; c < len; c++) {
            int px = text_x + c * GVI_CHAR_W;
            if (px + GVI_CHAR_W > cx + cw) break;

            bool is_block_cur = (is_cur && c == s_cur_col && s_mode != GVI_INSERT);

            if (is_block_cur) {
                fb_draw_char(px, py, s_lines[li][c], 0x080D16, 0x00FF88);
            } else {
                uint32_t fg = Color::TextNormal;
                char ch2 = s_lines[li][c];
                if (ch2 == '"' || ch2 == '\'') fg = 0xFF9944;
                else if (ch2 == '/' && c + 1 < len && s_lines[li][c+1] == '/') fg = 0x558877;
                fb_draw_char(px, py, ch2, fg, row_bg);
            }
        }

        if (is_cur && s_cur_col >= len && s_mode != GVI_INSERT) {
            int px = text_x + len * GVI_CHAR_W;
            if (px < cx + cw)
                fb_fill_rect(px, py, GVI_CHAR_W, GVI_CHAR_H, 0x00FF88);
        }

        if (is_cur && s_mode == GVI_INSERT) {
            int px = text_x + s_cur_col * GVI_CHAR_W;
            if (px < cx + cw) {
                uint32_t cur_col2 = (pit_uptime_s() % 2 == 0) ? 0x00FF88 : row_bg;
                fb_fill_rect(px, py, 2, GVI_CHAR_H, cur_col2);
            }
        }
    }

    //  Status bar 
    int sb_y = cy + text_h;
    fb_fill_gradient(cx, sb_y, cw, GVI_STATUS_H, 0x0F1F35, 0x091525);
    fb_fill_rect(cx, sb_y, cw, 1, Color::WinBorder);

    const char* mode_str = "NORMAL ";
    uint32_t    mode_col = 0x44BBFF;
    if (s_mode == GVI_INSERT) { mode_str = "INSERT "; mode_col = 0x44FF88; }
    if (s_mode == GVI_COMMAND){ mode_str = "";        mode_col = Color::Yellow; }

    fb_draw_text(cx + 4, sb_y + 5, mode_str, mode_col);

    if (s_mode == GVI_COMMAND) {
        char disp[66]; disp[0] = ':';
        k_strncpy(disp + 1, s_cmd, 63); disp[65] = '\0';
        fb_draw_text(cx + 4, sb_y + 5, disp, Color::TextNormal);
    } else if (s_status[0]) {
        fb_draw_text(cx + 64, sb_y + 5, s_status, Color::Yellow);
    }

    char pos[32];
    k_itoa(s_cur_row + 1, pos, 10);
    k_strcat(pos, ":");
    char tmp[12]; k_itoa(s_cur_col + 1, tmp, 10);
    k_strcat(pos, tmp);
    if (s_modified) k_strcat(pos, " [+]");
    int pw = fb_text_width(pos);
    fb_draw_text(cx + cw - pw - 8, sb_y + 5, pos, Color::TextDim);
}

//  WM key callback  — the core of the editor, no blocking I/O here

static void vi_key(int, char key, void*) {
    // Clear transient status on any keypress
    s_status[0] = '\0';

    //  INSERT mode 
    if (s_mode == GVI_INSERT) {
        if (key == 27) {                        // Esc → NORMAL
            s_mode = GVI_NORMAL;
            s_last_key = 0;
            if (s_cur_col > 0) s_cur_col--;
            clamp_col();
        } else if (key == '\b') {
            if (s_cur_col > 0) {
                s_cur_col--;
                delete_char_at(s_cur_row, s_cur_col);
            } else {
                merge_with_prev_line();
            }
        } else if (key == '\n' || key == '\r') {
            split_line_at_cursor();
        } else if (key >= 32 && (uint8_t)key < 127) {
            insert_char_at_cursor(key);
        }
        return;
    }

    //  COMMAND mode (:…) 
    if (s_mode == GVI_COMMAND) {
        if (key == 27) {
            s_mode = GVI_NORMAL; s_cmd_len = 0; s_cmd[0] = '\0';
        } else if (key == '\n' || key == '\r') {
            handle_command();
            if (s_wid >= 0) { s_mode = GVI_NORMAL; s_cmd_len = 0; }
        } else if (key == '\b') {
            if (s_cmd_len > 0) s_cmd[--s_cmd_len] = '\0';
            else { s_mode = GVI_NORMAL; s_cmd_len = 0; }
        } else if (key >= 32 && (uint8_t)key < 127 && s_cmd_len < 62) {
            s_cmd[s_cmd_len++] = key;
            s_cmd[s_cmd_len]   = '\0';
        }
        return;
    }

    //  NORMAL mode 

    // Two-key commands that depend on the previous key
    if (s_last_key == 'd' && key == 'd') {
        delete_current_line();
        s_last_key = 0; return;
    }
    if (s_last_key == 'g' && key == 'g') {
        s_cur_row = 0; s_cur_col = 0; s_last_key = 0; return;
    }

    // Keys that start a two-key sequence: remember and wait
    if (key == 'd' || key == 'g') {
        s_last_key = key;
        return;   // wait for second key
    }
    s_last_key = 0;

    switch (key) {
        //  Enter INSERT mode 
        case 'i':
            s_mode = GVI_INSERT;
            break;
        case 'I':                               // insert at line start
            s_cur_col = 0;
            s_mode = GVI_INSERT;
            break;
        case 'a':                               // append after cursor
            s_cur_col++;
            s_mode = GVI_INSERT;
            clamp_col();
            break;
        case 'A':                               // append at end of line
            s_cur_col = (int)k_strlen(s_lines[s_cur_row]);
            s_mode = GVI_INSERT;
            break;
        case 'o':                               // open line below
            s_cur_col = (int)k_strlen(s_lines[s_cur_row]);
            split_line_at_cursor();
            s_mode = GVI_INSERT;
            break;
        case 'O': {                             // open line above
            if (s_nlines >= GVI_MAX_LINES - 1) break;
            for (int i = s_nlines; i > s_cur_row; i--)
                k_strcpy(s_lines[i], s_lines[i - 1]);
            s_nlines++;
            s_lines[s_cur_row][0] = '\0';
            s_cur_col = 0;
            s_mode = GVI_INSERT;
            s_modified = true;
            break;
        }

        //  Enter COMMAND mode 
        case ':':
            s_mode    = GVI_COMMAND;
            s_cmd_len = 0;
            s_cmd[0]  = '\0';
            break;

        //  Movement — hjkl 
        case 'h':
            if (s_cur_col > 0) s_cur_col--;
            break;
        case 'l': {
            int len = (int)k_strlen(s_lines[s_cur_row]);
            if (s_cur_col < len - 1) s_cur_col++;
            break;
        }
        case 'j':
            if (s_cur_row < s_nlines - 1) { s_cur_row++; clamp_col(); }
            break;
        case 'k':
            if (s_cur_row > 0) { s_cur_row--; clamp_col(); }
            break;

        // The keyboard driver converts arrow extended codes to keypad chars.
        // Map them so arrow keys also work:
        case '8': if (s_cur_row > 0)         { s_cur_row--; clamp_col(); } break; // up
        case '2': if (s_cur_row < s_nlines-1){ s_cur_row++; clamp_col(); } break; // down
        case '4': if (s_cur_col > 0)           s_cur_col--;                break; // left
        case '6': {                                                                // right
            int len = (int)k_strlen(s_lines[s_cur_row]);
            if (s_cur_col < len - 1) s_cur_col++;
            break;
        }

        //  Line navigation 
        case '0': case '^':
            s_cur_col = 0;
            break;
        case '$': {
            int len = (int)k_strlen(s_lines[s_cur_row]);
            s_cur_col = (len > 0) ? len - 1 : 0;
            break;
        }
        case 'G':
            s_cur_row = s_nlines - 1; clamp_col();
            break;

        //  Word navigation 
        case 'w': {
            int len = (int)k_strlen(s_lines[s_cur_row]);
            while (s_cur_col < len - 1 && s_lines[s_cur_row][s_cur_col] != ' ')
                s_cur_col++;
            while (s_cur_col < len - 1 && s_lines[s_cur_row][s_cur_col] == ' ')
                s_cur_col++;
            break;
        }
        case 'b': {
            while (s_cur_col > 0 && s_lines[s_cur_row][s_cur_col - 1] == ' ')
                s_cur_col--;
            while (s_cur_col > 0 && s_lines[s_cur_row][s_cur_col - 1] != ' ')
                s_cur_col--;
            break;
        }

        //  Delete 
        case 'x':                               // delete char under cursor
            delete_char_at(s_cur_row, s_cur_col);
            clamp_col();
            break;
        case 'D': {                             // delete to end of line
            s_lines[s_cur_row][s_cur_col] = '\0';
            s_modified = true;
            break;
        }

        //  Page up/down (Ctrl+U / Ctrl+D style) 
        case 6: {   // Ctrl+F
            int vis = 10;
            s_cur_row += vis; clamp_row(); clamp_col();
            break;
        }
        case 2: {   // Ctrl+B
            int vis = 10;
            s_cur_row -= vis; clamp_row(); clamp_col();
            break;
        }

        //  Undo (not implemented) 
        case 'u':
            k_strcpy(s_status, "Undo not implemented. Use  :q!  to discard all changes.");
            break;

        default:
            break;
    }
}


//  WM close callback — guard against closing with unsaved changes

static void vi_close(int wid, void*) {
    if (s_modified) {
        // Don't close — show a warning in the status bar instead.
        // The window manager will not call wm_destroy because we didn't.
        k_strcpy(s_status, "Unsaved changes!  :wq to save & quit  or  :q! to discard.");
        // (next wm_render_all will pick this up via vi_draw)
        return;
    }
    wm_destroy(wid);
    s_wid = -1;
}


//  Public entry point

void gui_vi_open(const char* filename) {
    // If already open, just focus it
    if (s_wid >= 0) {
        wm_show(s_wid);
        wm_focus(s_wid);
        return;
    }

    // Resolve filename
    k_strncpy(s_filename, filename, VFS_MAX_PATH - 1);
    s_filename[VFS_MAX_PATH - 1] = '\0';
    // Strip leading spaces from shell tokenisation
    const char* fn = s_filename;
    while (*fn == ' ') fn++;
    if (fn != s_filename) k_strcpy(s_filename, fn);

    // Reset state
    s_cur_row  = 0;
    s_cur_col  = 0;
    s_scroll   = 0;
    s_mode     = GVI_NORMAL;
    s_cmd_len  = 0;
    s_cmd[0]   = '\0';
    s_modified = false;
    s_status[0]= '\0';
    s_last_key = 0;

    load_file();

    // Build window title
    char title[80];
    k_strcpy(title, "vi \xe2\x80\x94 ");      // "vi — "
    k_strncat(title, s_filename, (int)(sizeof(title) - 6));

    s_wid = wm_create(title, 60, 30, 940, 640);
    wm_set_callbacks(s_wid, vi_draw, vi_key, vi_close, nullptr, nullptr);
}