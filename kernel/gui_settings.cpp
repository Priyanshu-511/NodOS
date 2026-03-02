#include "../include/gui_settings.h"
#include "../include/settings_config.h"
#include "../include/wm.h"
#include "../include/fb.h"
#include "../include/vfs.h"
#include "../include/kstring.h"
#include "../include/pit.h"

//  Built-in themes
struct WpTheme  { const char* name; uint32_t top, bottom; };
struct TermTheme{ const char* name; uint32_t bg, fg, cursor; };

static const WpTheme WP_THEMES[] = {
    { "Classic",  0x1E3A5F, 0x0D1B2A },
    { "Midnight", 0x1A0A3A, 0x080318 },
    { "Forest",   0x0A2A12, 0x041208 },
    { "Sunset",   0x3A1808, 0x1A0802 },
    { "Slate",    0x1E1E2E, 0x0F0F1E },
    { "Ocean",    0x083A3A, 0x031C1C },
    { "Crimson",  0x3A0810, 0x1A0308 },
    { "Gold",     0x2A2208, 0x141102 },
};
static const int WP_THEME_COUNT = 8;

static const TermTheme TERM_THEMES[] = {
    { "Matrix",    0x0A0F1A, 0x00FF88, 0x00FF88 },
    { "Classic",   0x000022, 0x8888FF, 0xFFFFFF },
    { "Amber",     0x0A0800, 0xFFAA00, 0xFFCC44 },
    { "Dracula",   0x1E1429, 0xF8F8F2, 0xFF79C6 },
    { "Solarized", 0x002B36, 0x839496, 0x268BD2 },
    { "Phosphor",  0x000A00, 0x00CC00, 0x00FF00 },
};
static const int TERM_THEME_COUNT = 6;

//  VFS custom wallpapers  (/wallpapers/*.wal)
//  Format: line 1 = TOP hex colour,  line 2 = BOT hex colour
static const int MAX_VFS_WP = 16;
static char     s_vwp_name[MAX_VFS_WP][48];
static uint32_t s_vwp_top [MAX_VFS_WP];
static uint32_t s_vwp_bot [MAX_VFS_WP];
static int      s_vwp_cnt  = 0;

static uint32_t parse_hex(const char* s) {
    uint32_t v = 0;
    while (*s) {
        char c = *s++;
        uint32_t d;
        if      (c>='0'&&c<='9') d=(uint32_t)(c-'0');
        else if (c>='a'&&c<='f') d=(uint32_t)(c-'a'+10);
        else if (c>='A'&&c<='F') d=(uint32_t)(c-'A'+10);
        else break;
        v=(v<<4)|d;
    }
    return v;
}

static void scan_vfs_wallpapers() {
    s_vwp_cnt = 0;
    char names[VFS_MAX_ENTRIES][VFS_MAX_PATH];
    bool is_dir[VFS_MAX_ENTRIES];
    int cnt = 0;
    vfs_listdir("/wallpapers", names, is_dir, &cnt);

    static char fbuf[256];
    for (int i = 0; i < cnt && s_vwp_cnt < MAX_VFS_WP; i++) {
        if (is_dir[i]) continue;
        int len = (int)k_strlen(names[i]);
        if (len < 5 || k_strcmp(names[i]+len-4, ".wal") != 0) continue;

        char path[VFS_MAX_PATH];
        k_strcpy(path, "/wallpapers/");
        k_strcat(path, names[i]);
        if (vfs_read(path, fbuf, 255) < 0) continue;
        fbuf[255] = '\0';

        // Line 1 = top colour, line 2 = bottom colour
        char* line2 = nullptr;
        for (int j = 0; fbuf[j]; j++) {
            if (fbuf[j] == '\n') { fbuf[j] = '\0'; line2 = fbuf + j + 1; break; }
        }
        if (!line2) continue;
        for (int j = 0; line2[j]; j++) { if (line2[j]=='\n') { line2[j]='\0'; break; } }

        s_vwp_top[s_vwp_cnt] = parse_hex(fbuf);
        s_vwp_bot[s_vwp_cnt] = parse_hex(line2);

        // Display name = filename without .wal
        k_strncpy(s_vwp_name[s_vwp_cnt], names[i], 47);
        int nlen = (int)k_strlen(s_vwp_name[s_vwp_cnt]);
        if (nlen > 4) s_vwp_name[s_vwp_cnt][nlen-4] = '\0';

        s_vwp_cnt++;
    }
}

//  VFS custom terminal themes  (/themes/*.tml)
//  Format: line 1 = BG hex,  line 2 = FG hex,  line 3 = CURSOR hex
static const int MAX_VFS_TM = 16;
static char     s_vtm_name  [MAX_VFS_TM][48];
static uint32_t s_vtm_bg    [MAX_VFS_TM];
static uint32_t s_vtm_fg    [MAX_VFS_TM];
static uint32_t s_vtm_cursor[MAX_VFS_TM];
static int      s_vtm_cnt   = 0;

static void scan_vfs_themes() {
    s_vtm_cnt = 0;
    char names[VFS_MAX_ENTRIES][VFS_MAX_PATH];
    bool is_dir[VFS_MAX_ENTRIES];
    int cnt = 0;
    vfs_listdir("/themes", names, is_dir, &cnt);

    static char fbuf[256];
    for (int i = 0; i < cnt && s_vtm_cnt < MAX_VFS_TM; i++) {
        if (is_dir[i]) continue;
        int len = (int)k_strlen(names[i]);
        if (len < 5 || k_strcmp(names[i]+len-4, ".tml") != 0) continue;

        char path[VFS_MAX_PATH];
        k_strcpy(path, "/themes/");
        k_strcat(path, names[i]);
        if (vfs_read(path, fbuf, 255) < 0) continue;
        fbuf[255] = '\0';

        // Split into up to 3 lines
        char* lines[3] = { fbuf, nullptr, nullptr };
        int   lc = 1;
        for (int j = 0; fbuf[j] && lc < 3; j++) {
            if (fbuf[j] == '\n') {
                fbuf[j] = '\0';
                lines[lc++] = fbuf + j + 1;
            }
        }
        // Trim trailing newline on last line
        for (int k2 = 0; k2 < 3 && lines[k2]; k2++) {
            for (int j = 0; lines[k2][j]; j++)
                if (lines[k2][j] == '\n') { lines[k2][j] = '\0'; break; }
        }
        if (lc < 3 || !lines[2]) continue;

        s_vtm_bg    [s_vtm_cnt] = parse_hex(lines[0]);
        s_vtm_fg    [s_vtm_cnt] = parse_hex(lines[1]);
        s_vtm_cursor[s_vtm_cnt] = parse_hex(lines[2]);

        // Display name = filename without .tml
        k_strncpy(s_vtm_name[s_vtm_cnt], names[i], 47);
        int nlen = (int)k_strlen(s_vtm_name[s_vtm_cnt]);
        if (nlen > 4) s_vtm_name[s_vtm_cnt][nlen-4] = '\0';

        s_vtm_cnt++;
    }
}


//  Window state
static int  s_wid          = -1;
static int  s_tab          = 0;   // 0=wallpaper 1=terminal 2=system

// Selections (-1 = unrecognised / custom)
static int  s_wp_sel       = -1;
static int  s_term_sel     = -1;
static int  s_vterm_sel    = -1;  // index into VFS themes

// System tab – in-place hostname editor
static char s_host_buf[32];
static int  s_host_len     = 0;
static bool s_host_editing = false;

// Cached client dims (set each draw, used by mouse handler)
static int  s_cw = 0, s_ch = 0;

//  Layout constants
static const int SIDEBAR_W  = 130;
static const int TAB_H      = 40;
static const int TAB_OFFS_Y = 38;  // first tab button starts here (below header)

static const char* TAB_LABELS[] = { "Wallpaper", "Terminal", "System" };
static const uint32_t TAB_ACCENT[] = { 0x3D6FCC, 0x00FF88, 0xFFDD44 };

// Infer which themes match current g_settings 
static void sync_selections() {
    s_wp_sel = -1;
    for (int i = 0; i < WP_THEME_COUNT; i++)
        if (g_settings.wp_top == WP_THEMES[i].top &&
            g_settings.wp_bottom == WP_THEMES[i].bottom) { s_wp_sel = i; break; }
    if (s_wp_sel < 0)
        for (int i = 0; i < s_vwp_cnt; i++)
            if (g_settings.wp_top == s_vwp_top[i] &&
                g_settings.wp_bottom == s_vwp_bot[i]) { s_wp_sel = WP_THEME_COUNT + i; break; }

    s_term_sel  = -1;
    s_vterm_sel = -1;
    for (int i = 0; i < TERM_THEME_COUNT; i++)
        if (g_settings.term_bg == TERM_THEMES[i].bg &&
            g_settings.term_fg == TERM_THEMES[i].fg &&
            g_settings.term_cursor == TERM_THEMES[i].cursor)
            { s_term_sel = i; break; }
    if (s_term_sel < 0)
        for (int i = 0; i < s_vtm_cnt; i++)
            if (g_settings.term_bg == s_vtm_bg[i] &&
                g_settings.term_fg == s_vtm_fg[i] &&
                g_settings.term_cursor == s_vtm_cursor[i])
                { s_vterm_sel = i; break; }

    k_strncpy(s_host_buf, g_settings.hostname, 31);
    s_host_buf[31] = '\0';
    s_host_len = (int)k_strlen(s_host_buf);
    s_host_editing = false;
}

//  Draw helpers
static void draw_sidebar(int cx, int cy, int ch) {
    fb_fill_gradient(cx, cy, SIDEBAR_W, ch, 0x0A1830, 0x050F1E);
    fb_fill_rect(cx + SIDEBAR_W - 1, cy, 1, ch, Color::WinBorder);

    // Header
    fb_fill_gradient(cx, cy, SIDEBAR_W-1, 36, Color::WinTitle, Color::DarkBlue);
    fb_draw_text(cx + 10, cy + 14, "Settings", Color::White);
    fb_fill_rect(cx, cy + 36, SIDEBAR_W-1, 1, Color::WinBorder);

    for (int i = 0; i < 3; i++) {
        int ty = cy + TAB_OFFS_Y + i * TAB_H;
        bool active = (i == s_tab);

        if (active) {
            fb_fill_gradient(cx, ty, SIDEBAR_W-1, TAB_H, 0x1E3A6A, 0x142850);
            fb_fill_rect(cx, ty, 3, TAB_H, TAB_ACCENT[i]);
        }

        fb_fill_circle(cx + 20, ty + TAB_H/2, 5, TAB_ACCENT[i]);
        fb_draw_text(cx + 34, ty + (TAB_H-8)/2,
                     TAB_LABELS[i],
                     active ? Color::White : Color::TextDim);

        fb_fill_rect(cx+4, ty + TAB_H - 1, SIDEBAR_W-8, 1, 0x1A2A40);
    }
}

static void draw_apply_btn(int cx, int cy, int cw, int ch) {
    int bx = cx + cw - 90;
    int by = cy + ch - 38;
    fb_fill_gradient(bx, by, 80, 26, 0x2A6ACC, 0x1A4A9A);
    fb_draw_rect    (bx, by, 80, 26, Color::WinBorder);
    fb_draw_text    (bx + 20, by + 9, "Apply", Color::White);
}

// Wallpaper tab 
static void draw_wallpaper_tab(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, 0x08131F);

    fb_draw_text(cx + 8, cy + 10, "Choose Wallpaper", Color::Cyan);
    fb_fill_rect(cx + 8, cy + 24, cw - 16, 1, Color::WinBorder);
    fb_draw_text(cx + 8, cy + 30, "Built-in Themes", Color::TextDim);

    const int SW = 96, SH = 50, GAP = 8, COLS = 4, LABEL_H = 14;
    int gy = cy + 46;

    for (int i = 0; i < WP_THEME_COUNT; i++) {
        int col = i % COLS;
        int row = i / COLS;
        int sx  = cx + 8 + col * (SW + GAP);
        int sy  = gy  + row * (SH + LABEL_H + 4);
        bool sel = (s_wp_sel == i);

        fb_fill_gradient(sx, sy, SW, SH, WP_THEMES[i].top, WP_THEMES[i].bottom);

        // Border (thicker yellow when selected)
        uint32_t border_col = sel ? Color::Yellow : Color::WinBorder;
        fb_draw_rect(sx, sy, SW, SH, border_col);
        if (sel) {
            fb_draw_rect(sx+1, sy+1, SW-2, SH-2, Color::Yellow);
            // tick mark in top-right
            fb_fill_rect(sx + SW - 14, sy + 4, 10, 10, Color::Yellow);
            fb_draw_text(sx + SW - 12, sy + 4, "OK", Color::Black);
        }

        // Name under swatch
        int lx = sx + SW/2 - (int)(k_strlen(WP_THEMES[i].name) * 4);
        fb_draw_text(lx, sy + SH + 2, WP_THEMES[i].name,
                     sel ? Color::Yellow : Color::TextNormal);
    }

    // VFS custom section
    int custom_y = gy + 2 * (SH + LABEL_H + 4) + 6;
    fb_fill_rect(cx + 8, custom_y, cw - 16, 1, Color::MidGrey);
    fb_draw_text(cx + 8, custom_y + 4, "Custom  (/wallpapers/*.wal)", Color::TextDim);

    if (s_vwp_cnt == 0) {
        fb_draw_text(cx + 8, custom_y + 18,
                     "No .wal files found. Create one: with mkwal",
                     0x506070);
    } else {
        for (int i = 0; i < s_vwp_cnt; i++) {
            int ry  = custom_y + 18 + i * 30;
            int ri  = WP_THEME_COUNT + i;
            bool sel = (s_wp_sel == ri);
            // mini swatch
            fb_fill_gradient(cx + 8, ry + 3, 52, 22, s_vwp_top[i], s_vwp_bot[i]);
            fb_draw_rect(cx + 8, ry + 3, 52, 22,
                         sel ? Color::Yellow : Color::MidGrey);
            fb_draw_text(cx + 68, ry + 8, s_vwp_name[i],
                         sel ? Color::Yellow : Color::TextNormal);
        }
    }

    draw_apply_btn(cx, cy, cw, ch);
}

// Terminal tab  (swatch-card layout matching wallpaper tab)
static void draw_terminal_tab(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, 0x08131F);

    fb_draw_text(cx + 8, cy + 10, "Terminal Colors", Color::Cyan);
    fb_fill_rect(cx + 8, cy + 24, cw - 16, 1, Color::WinBorder);
    fb_draw_text(cx + 8, cy + 30, "Built-in Themes", Color::TextDim);

    // Card layout: each card shows a mini terminal preview (bg fill + two
    // sample text lines in fg colour + a cursor block).
    const int CW = 120, CH = 60, GAP = 8, COLS = 3, LABEL_H = 14;
    int gy = cy + 46;

    for (int i = 0; i < TERM_THEME_COUNT; i++) {
        int col = i % COLS;
        int row = i / COLS;
        int sx  = cx + 8 + col * (CW + GAP);
        int sy  = gy  + row * (CH + LABEL_H + 4);
        bool sel = (s_term_sel == i);

        // Card background (terminal bg colour)
        fb_fill_rect(sx, sy, CW, CH, TERM_THEMES[i].bg);

        // Sample content inside card
        int tx = sx + 5, ty = sy + 6;
        // hostname> line
        char pline[24];
        k_strcpy(pline, "nodos> ");
        fb_draw_text(tx, ty,      pline,        TERM_THEMES[i].fg);
        fb_draw_text(tx, ty + 13, "ls /home",   TERM_THEMES[i].fg);
        // cursor block
        fb_fill_rect(tx, ty + 26, 7, 11, TERM_THEMES[i].cursor);
        // small bg/fg/cursor colour dots at bottom-right
        fb_fill_rect(sx + CW - 28, sy + CH - 10, 8, 8, TERM_THEMES[i].bg);
        fb_draw_rect(sx + CW - 28, sy + CH - 10, 8, 8, Color::MidGrey);
        fb_fill_rect(sx + CW - 18, sy + CH - 10, 8, 8, TERM_THEMES[i].fg);
        fb_fill_rect(sx + CW -  8, sy + CH - 10, 8, 8, TERM_THEMES[i].cursor);

        // Selection border (thick yellow, like wallpaper tab)
        uint32_t bcol = sel ? Color::Yellow : Color::WinBorder;
        fb_draw_rect(sx, sy, CW, CH, bcol);
        if (sel) {
            fb_draw_rect(sx+1, sy+1, CW-2, CH-2, Color::Yellow);
            fb_fill_rect(sx + CW - 14, sy + 4, 22, 10, Color::Yellow);
            fb_draw_text(sx + CW - 12, sy + 4, "OK", Color::Black);
        }

        // Label below card
        int lx = sx + CW/2 - (int)(k_strlen(TERM_THEMES[i].name) * 4);
        fb_draw_text(lx, sy + CH + 2, TERM_THEMES[i].name,
                     sel ? Color::Yellow : Color::TextNormal);
    }

    //  VFS custom themes section (/themes/*.tml) 
    int rows_used = (TERM_THEME_COUNT + COLS - 1) / COLS;
    int custom_y = gy + rows_used * (CH + LABEL_H + 4) + 6;
    fb_fill_rect(cx + 8, custom_y, cw - 16, 1, Color::MidGrey);
    fb_draw_text(cx + 8, custom_y + 4, "Custom  (/themes/*.tml)", Color::TextDim);

    if (s_vtm_cnt == 0) {
        fb_draw_text(cx + 8, custom_y + 18,
                     "No .tml files found. Create one: with mktml",
                     0x506070);
    } else {
        for (int i = 0; i < s_vtm_cnt; i++) {
            int ry   = custom_y + 18 + i * 36;
            bool sel = (s_vterm_sel == i);
            // Mini terminal preview strip
            int sw = CW, sh = 28;
            fb_fill_rect(cx + 8, ry, sw, sh, s_vtm_bg[i]);
            int tx2 = cx + 13, ty2 = ry + 4;
            char pline2[16]; k_strcpy(pline2, "nodos> ");
            fb_draw_text(tx2, ty2,      pline2,     s_vtm_fg[i]);
            fb_fill_rect(tx2, ty2 + 13, 7, 8,       s_vtm_cursor[i]);
            // colour dots
            fb_fill_rect(cx + 8 + sw - 28, ry + sh - 10, 8, 8, s_vtm_bg[i]);
            fb_draw_rect(cx + 8 + sw - 28, ry + sh - 10, 8, 8, Color::MidGrey);
            fb_fill_rect(cx + 8 + sw - 18, ry + sh - 10, 8, 8, s_vtm_fg[i]);
            fb_fill_rect(cx + 8 + sw -  8, ry + sh - 10, 8, 8, s_vtm_cursor[i]);
            // Border
            fb_draw_rect(cx + 8, ry, sw, sh, sel ? Color::Yellow : Color::MidGrey);
            if (sel) fb_draw_rect(cx + 9, ry + 1, sw-2, sh-2, Color::Yellow);
            // Name
            fb_draw_text(cx + 8 + sw + 8, ry + 8, s_vtm_name[i],
                         sel ? Color::Yellow : Color::TextNormal);
        }
    }

    draw_apply_btn(cx, cy, cw, ch);
}

//  System tab 
static void draw_system_tab(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, 0x08131F);

    fb_draw_text(cx + 8, cy + 10, "System Settings", Color::Cyan);
    fb_fill_rect(cx + 8, cy + 24, cw - 16, 1, Color::WinBorder);

    //  Hostname section 
    fb_draw_text(cx + 8, cy + 36, "Prompt Name", Color::TextNormal);
    fb_draw_text(cx + 8, cy + 50,
                 "Sets the name shown in the shell prompt (max 31 chars).", Color::TextDim);

    int bx = cx + 8, by = cy + 68, bw = 210, bh = 28;
    fb_fill_rect(bx, by, bw, bh,  s_host_editing ? 0x0A1830 : 0x050F1E);
    fb_draw_rect(bx, by, bw, bh,  s_host_editing ? Color::Cyan : Color::WinBorder);

    // Show text + blinking cursor
    fb_draw_text(bx + 6, by + 10, s_host_buf, Color::White);
    if (s_host_editing) {
        bool blink = (pit_uptime_s() % 2 == 0);
        if (blink) {
            int cur_x = bx + 6 + s_host_len * 8;
            fb_fill_rect(cur_x, by + 8, 2, 14, Color::Cyan);
        }
    }

    fb_draw_text(bx + bw + 8, by + 10,
                 s_host_editing ? "type + Enter" : "click to edit",
                 Color::TextDim);

    //  Live previews 
    fb_draw_text(cx + 8, cy + 110, "Shell (VGA text mode):", Color::TextDim);
    {
        char line[64];
        k_strcpy(line, s_host_buf);
        k_strcat(line, "@kernel:/$ _");
        fb_fill_rect(cx + 8, cy + 124, cw - 16, 26, 0x040A0E);
        fb_draw_rect(cx + 8, cy + 124, cw - 16, 26, Color::WinBorder);
        fb_draw_text(cx + 14, cy + 133, line, 0x00FF88);
    }

    fb_draw_text(cx + 8, cy + 162, "GUI Terminal:", Color::TextDim);
    {
        char line[64];
        k_strcpy(line, s_host_buf);
        k_strcat(line, "> _");
        fb_fill_rect(cx + 8, cy + 176, cw - 16, 26, g_settings.term_bg);
        fb_draw_rect(cx + 8, cy + 176, cw - 16, 26, Color::WinBorder);
        fb_draw_text(cx + 14, cy + 185, line, g_settings.term_fg);
    }

    //  Disk info 
    fb_fill_rect(cx + 8, cy + 216, cw - 16, 1, Color::MidGrey);
    fb_draw_text(cx + 8, cy + 222, "Settings are saved to /etc/settings.cfg", Color::TextDim);

    draw_apply_btn(cx, cy, cw, ch);
}

// 
//  WM callbacks
// 
void gui_settings_draw(int, int cx, int cy, int cw, int ch, void*) {
    s_cw = cw; s_ch = ch;

    fb_fill_rect(cx, cy, cw, ch, 0x08131F);

    // Sidebar
    draw_sidebar(cx, cy, ch);

    // Content pane
    int px = cx + SIDEBAR_W + 1;
    int pw = cw - SIDEBAR_W - 1;

    if (s_tab == 0) draw_wallpaper_tab(px, cy, pw, ch);
    else if (s_tab == 1) draw_terminal_tab(px, cy, pw, ch);
    else                 draw_system_tab  (px, cy, pw, ch);
}

void gui_settings_key(int, char key, void*) {
    if (s_tab != 2 || !s_host_editing) return;

    if (key == '\n' || key == '\r') {
        s_host_editing = false;
        return;
    }
    if (key == 27) {   // ESC — cancel
        k_strncpy(s_host_buf, g_settings.hostname, 31);
        s_host_len    = (int)k_strlen(s_host_buf);
        s_host_editing = false;
        return;
    }
    if (key == '\b') {
        if (s_host_len > 0) { s_host_buf[--s_host_len] = '\0'; }
        return;
    }
    // Printable ASCII, max 31 chars, no spaces in hostname
    if (key >= 32 && key < 127 && key != ' ' && s_host_len < 31) {
        s_host_buf[s_host_len++] = key;
        s_host_buf[s_host_len]   = '\0';
    }
}

void gui_settings_mouse(int, int mx, int my, bool left, bool, void*) {
    if (!left) return;

    //  Sidebar tab click 
    if (mx < SIDEBAR_W) {
        for (int i = 0; i < 3; i++) {
            int ty = TAB_OFFS_Y + i * TAB_H;
            if (my >= ty && my < ty + TAB_H) {
                s_tab = i;
                s_host_editing = false;
            }
        }
        return;
    }

    // Content-local coordinates
    int lx = mx - (SIDEBAR_W + 1);
    int ly = my;

    //  Apply button  (bottom-right of content pane) 
    int pw = s_cw - SIDEBAR_W - 1;
    int abx = pw - 90, aby = s_ch - 38;
    if (lx >= abx && lx < abx + 80 && ly >= aby && ly < aby + 26) {
        // Commit changes from current tab
        if (s_tab == 0 && s_wp_sel >= 0) {
            if (s_wp_sel < WP_THEME_COUNT) {
                g_settings.wp_top    = WP_THEMES[s_wp_sel].top;
                g_settings.wp_bottom = WP_THEMES[s_wp_sel].bottom;
                k_strncpy(g_settings.wp_name, WP_THEMES[s_wp_sel].name, 63);
            } else {
                int vi = s_wp_sel - WP_THEME_COUNT;
                g_settings.wp_top    = s_vwp_top[vi];
                g_settings.wp_bottom = s_vwp_bot[vi];
                k_strncpy(g_settings.wp_name, s_vwp_name[vi], 63);
            }
        } else if (s_tab == 1) {
            if (s_term_sel >= 0) {
                g_settings.term_bg     = TERM_THEMES[s_term_sel].bg;
                g_settings.term_fg     = TERM_THEMES[s_term_sel].fg;
                g_settings.term_cursor = TERM_THEMES[s_term_sel].cursor;
            } else if (s_vterm_sel >= 0) {
                g_settings.term_bg     = s_vtm_bg    [s_vterm_sel];
                g_settings.term_fg     = s_vtm_fg    [s_vterm_sel];
                g_settings.term_cursor = s_vtm_cursor[s_vterm_sel];
            }
        } else if (s_tab == 2 && s_host_len > 0) {
            k_strncpy(g_settings.hostname, s_host_buf, 31);
            s_host_editing = false;
        }
        settings_save();
        return;
    }

    //  Tab-specific hit tests 
    if (s_tab == 0) {
        // Built-in wallpaper swatches
        const int SW = 96, SH = 50, GAP = 8, COLS = 4, LABEL_H = 14;
        int gy = 46;
        for (int i = 0; i < WP_THEME_COUNT; i++) {
            int col = i % COLS, row = i / COLS;
            int sx  = 8 + col * (SW + GAP);
            int sy  = gy + row * (SH + LABEL_H + 4);
            if (lx >= sx && lx < sx + SW && ly >= sy && ly < sy + SH) {
                s_wp_sel = i; return;
            }
        }
        // VFS wallpapers
        int custom_y = gy + 2 * (SH + LABEL_H + 4) + 6;
        for (int i = 0; i < s_vwp_cnt; i++) {
            int ry = custom_y + 18 + i * 30;
            if (ly >= ry && ly < ry + 28) {
                s_wp_sel = WP_THEME_COUNT + i; return;
            }
        }
    } else if (s_tab == 1) {
        // Built-in theme cards (same grid maths as draw_terminal_tab)
        const int CW = 120, CH = 60, GAP = 8, COLS = 3, LABEL_H = 14;
        int gy = 46;
        bool hit = false;
        for (int i = 0; i < TERM_THEME_COUNT && !hit; i++) {
            int col = i % COLS, row = i / COLS;
            int sx  = 8 + col * (CW + GAP);
            int sy  = gy + row * (CH + LABEL_H + 4);
            if (lx >= sx && lx < sx + CW && ly >= sy && ly < sy + CH) {
                s_term_sel  = i;
                s_vterm_sel = -1;
                hit = true;
            }
        }
        // VFS custom theme strips
        if (!hit) {
            int rows_used = (TERM_THEME_COUNT + COLS - 1) / COLS;
            int custom_y = gy + rows_used * (CH + LABEL_H + 4) + 6;
            for (int i = 0; i < s_vtm_cnt && !hit; i++) {
                int ry = custom_y + 18 + i * 36;
                if (ly >= ry && ly < ry + 28) {
                    s_vterm_sel = i;
                    s_term_sel  = -1;
                    hit = true;
                }
            }
        }
    } else if (s_tab == 2) {
        // Hostname input box
        int bx = 8, by = 68, bw = 210, bh = 28;
        if (lx >= bx && lx < bx + bw && ly >= by && ly < by + bh) {
            s_host_editing = true;
        } else {
            s_host_editing = false;
        }
    }
}

void gui_settings_close(int wid, void*) {
    wm_destroy(wid);
    s_wid = -1;
}

// 
//  Open (or re-focus)
// 
void gui_settings_open() {
    if (s_wid >= 0) { wm_show(s_wid); wm_focus(s_wid); return; }

    scan_vfs_wallpapers();
    scan_vfs_themes();
    sync_selections();

    s_wid = wm_create("Settings", 180, 140, 620, 440);
    wm_set_callbacks(s_wid,
                     gui_settings_draw,
                     gui_settings_key,
                     gui_settings_close,
                     gui_settings_mouse,
                     nullptr);
}