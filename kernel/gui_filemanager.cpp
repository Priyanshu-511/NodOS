#include "../include/gui_filemanager.h"
#include "../include/wm.h"
#include "../include/fb.h"
#include "../include/vfs.h"
#include "../include/kstring.h"
#include "../include/heap.h"

//  State
static int   s_wid        = -1;
static int   s_selected   = -1;    
static int   s_scroll     = 0;     
static bool  s_dirty      = true;

static char       s_entries[VFS_MAX_ENTRIES][VFS_MAX_PATH];
static bool       s_is_dir [VFS_MAX_ENTRIES];
static int        s_count  = 0;
static char       s_cwd   [VFS_MAX_PATH];
static char       s_status[128];   

// Preview pane content
static char s_preview[VFS_MAX_FILESIZE + 1];
static bool s_has_preview = false;

// History state
static char s_history[32][VFS_MAX_PATH];
static int  s_hist_idx = -1;
static int  s_hist_max = -1;

// Core Navigation and state management
static void refresh() {
    vfs_getcwd(s_cwd, VFS_MAX_PATH);
    s_count = 0;
    vfs_listdir(s_cwd, s_entries, s_is_dir, &s_count);
    s_selected = -1;
    s_scroll   = 0;
    s_has_preview = false;
    s_dirty    = true;
    k_strcpy(s_status, "");
}

static void go_to(const char* path) {
    if (vfs_chdir(path) == 0) {
        s_hist_idx++;
        if (s_hist_idx >= 32) s_hist_idx = 31; // Clamp history
        vfs_getcwd(s_history[s_hist_idx], VFS_MAX_PATH);
        s_hist_max = s_hist_idx; // Wipe forward history
        refresh();
    }
}

static void go_back() {
    if (s_hist_idx > 0) {
        s_hist_idx--;
        vfs_chdir(s_history[s_hist_idx]);
        refresh();
    }
}

static void go_forward() {
    if (s_hist_idx < s_hist_max) {
        s_hist_idx++;
        vfs_chdir(s_history[s_hist_idx]);
        refresh();
    }
}

// Preview a file in the right pane (only if it's not a directory)
static void preview_file(const char* name) {
    char path[VFS_MAX_PATH];
    vfs_resolve(name, path, VFS_MAX_PATH);

    s_preview[0] = '\0';
    s_has_preview = false;

    uint32_t sz = vfs_size(path);
    if (sz == 0) { k_strcpy(s_preview, "(empty file)"); s_has_preview = true; return; }
    if (sz > VFS_MAX_FILESIZE) { k_strcpy(s_preview, "(file too large to preview)"); s_has_preview = true; return; }

    if (vfs_read(path, s_preview, VFS_MAX_FILESIZE) >= 0) {
        s_preview[VFS_MAX_FILESIZE] = '\0';
        s_has_preview = true;
    }
}

// Draw the file manager UI 
static const int ROW_H      = 20;
static const int ADDR_H     = 24;
static const int STATUS_H   = 20;
static const int SPLIT      = 240;  

void gui_filemanager_draw(int, int cx, int cy, int cw, int ch, void*) {
    // Address bar with Back / Forward buttons
    fb_fill_gradient(cx, cy, cw, ADDR_H, 0x1A2B4A, 0x0F1F35);

    bool can_back = (s_hist_idx > 0);
    bool can_fwd  = (s_hist_idx < s_hist_max);
    
    fb_draw_text(cx + 8, cy + 8, "<", can_back ? Color::White : Color::DarkGrey);
    fb_draw_text(cx + 24, cy + 8, ">", can_fwd ? Color::White : Color::DarkGrey);
    fb_draw_text(cx + 44, cy + 8, s_cwd, Color::Cyan);
    fb_fill_rect(cx, cy + ADDR_H - 1, cw, 1, Color::WinBorder);

    int list_y = cy + ADDR_H;
    int list_h = ch - ADDR_H - STATUS_H;

    // Left pane: directory listing 
    fb_fill_rect(cx, list_y, SPLIT, list_h, 0x0A1525);
    fb_fill_rect(cx + SPLIT, list_y, 1, list_h, Color::WinBorder);

    int rows_visible = list_h / ROW_H;
    for (int i = s_scroll; i < s_count && i < s_scroll + rows_visible; i++) {
        int ry  = list_y + (i - s_scroll) * ROW_H;
        bool sel = (i == s_selected);

        if (sel)
            fb_fill_gradient(cx, ry, SPLIT, ROW_H, Color::Highlight, 0x2A5A8A);
        else if (i % 2 == 0)
            fb_fill_rect(cx, ry, SPLIT, ROW_H, 0x0C1828);

        uint32_t icon_col = s_is_dir[i] ? Color::Yellow : Color::LightBlue;
        fb_fill_circle(cx + 10, ry + ROW_H/2, 5, icon_col);
        if (s_is_dir[i]) {
            fb_fill_rect(cx + 6, ry + ROW_H/2 - 5, 5, 3, icon_col);
        }

        char name[32];
        k_strncpy(name, s_entries[i], 31);
        name[31] = '\0';
        fb_draw_text(cx + 20, ry + (ROW_H - 8)/2, name,
                     sel ? Color::White : (s_is_dir[i] ? Color::Yellow : Color::TextNormal));
    }

    if (s_count > rows_visible) {
        int sb_x = cx + SPLIT - 5;
        fb_fill_rect(sb_x, list_y, 4, list_h, Color::DarkGrey);
        int th = list_h * rows_visible / s_count;
        if (th < 10) th = 10;
        int ty = list_y + (list_h - th) * s_scroll / (s_count - rows_visible + 1);
        fb_fill_rect(sb_x, ty, 4, th, Color::MidGrey);
    }

    // Right pane: preview 
    int pane_x = cx + SPLIT + 1;
    int pane_w = cw - SPLIT - 1;
    fb_fill_rect(pane_x, list_y, pane_w, list_h, Color::TermBg);

    if (s_has_preview && s_selected >= 0) {
        fb_draw_text(pane_x + 6, list_y + 4, s_entries[s_selected], Color::Cyan);
        fb_fill_rect(pane_x, list_y + 18, pane_w, 1, Color::MidGrey);

        int px = pane_x + 4, py = list_y + 22;
        int max_cols = (pane_w - 8) / 8;
        int col = 0;
        for (const char* p = s_preview; *p && py < list_y + list_h - 8; p++) {
            if (*p == '\n' || col >= max_cols) {
                px = pane_x + 4; py += 10; col = 0;
                if (*p == '\n') continue;
            }
            fb_draw_char(px, py, *p, Color::TextNormal, Color::TermBg);
            px += 8; col++;
        }
    } else if (s_selected >= 0 && s_is_dir[s_selected]) {
        fb_draw_text(pane_x + 6, list_y + 4, s_entries[s_selected], Color::Yellow);
        fb_fill_rect(pane_x, list_y + 18, pane_w, 1, Color::MidGrey);
        fb_draw_text(pane_x + 6, list_y + 28, "Directory", Color::TextDim);
        
        char sub[64];
        char tmp[VFS_MAX_PATH];
        vfs_resolve(s_entries[s_selected], tmp, VFS_MAX_PATH);
        static char sub_names[VFS_MAX_ENTRIES][VFS_MAX_PATH];
        static bool sub_dir  [VFS_MAX_ENTRIES];
        int sub_cnt = 0;
        vfs_listdir(tmp, sub_names, sub_dir, &sub_cnt);
        k_strcpy(sub, "Contains ");
        char numstr[8]; k_itoa(sub_cnt, numstr, 10);
        k_strcat(sub, numstr);
        k_strcat(sub, " items");
        fb_draw_text(pane_x + 6, list_y + 44, sub, Color::TextNormal);
    } else {
        fb_draw_text(pane_x + pane_w/2 - 40, list_y + list_h/2 - 4,
                     "Select a file", Color::TextDim);
    }

    //  Status bar 
    int sb_y = cy + ch - STATUS_H;
    fb_fill_rect(cx, sb_y, cw, STATUS_H, 0x0A1525);
    fb_fill_rect(cx, sb_y, cw, 1, Color::WinBorder);

    char count_str[32];
    k_itoa(s_count, count_str, 10);
    k_strcat(count_str, " items");
    fb_draw_text(cx + 6, sb_y + 6, count_str, Color::TextDim);

    if (s_status[0])
        fb_draw_text(cx + 100, sb_y + 6, s_status, Color::Green);

    char disk_str[48];
    uint32_t used_sec = vfs_get_used_sectors();
    k_itoa((int)(used_sec / 2048), disk_str, 10);  
    k_strcat(disk_str, " MB used");
    int dw = fb_text_width(disk_str);
    fb_draw_text(cx + cw - dw - 8, sb_y + 6, disk_str, Color::TextDim);
}

// Key handling 
void gui_filemanager_key(int, char key, void*) {
    int rows_visible = 20; 
    if (key == '\x1B') { // ESC
        go_to("..");
        return;
    }
    if (key == 'j' || key == 'B') {   
        if (s_selected < s_count - 1) {
            s_selected++;
            if (s_selected >= s_scroll + rows_visible) s_scroll++;
        }
    } else if (key == 'k' || key == 'A') { 
        if (s_selected > 0) {
            s_selected--;
            if (s_selected < s_scroll) s_scroll--;
        }
    } else if (key == '\n' || key == '\r') {
        if (s_selected >= 0) {
            if (s_is_dir[s_selected]) {
                go_to(s_entries[s_selected]);
            } else {
                preview_file(s_entries[s_selected]);
            }
        }
    } else if (key == 127 || key == 'd') { // DEL
        if (s_selected >= 0 && !s_is_dir[s_selected]) {
            char path[VFS_MAX_PATH];
            vfs_resolve(s_entries[s_selected], path, VFS_MAX_PATH);
            vfs_delete(path);
            k_strcpy(s_status, "Deleted: ");
            k_strncat(s_status, s_entries[s_selected], sizeof(s_status));
            refresh();
        }
    }
    s_dirty = true;
}

// Mouse handling 
void gui_filemanager_mouse(int wid, int cx, int cy, bool left, bool, void*) {
    (void)wid;
    if (!left) return;

    if (cy < ADDR_H) {
        // Hitboxes for Back and Forward buttons
        if (cx >= 4 && cx <= 16 && s_hist_idx > 0) go_back();
        else if (cx >= 20 && cx <= 32 && s_hist_idx < s_hist_max) go_forward();
        return;
    }
    
    int rel_y = cy - ADDR_H;
    int idx   = rel_y / ROW_H + s_scroll;
    
    if (idx >= 0 && idx < s_count) {
        if (s_selected == idx && left) {
            if (s_is_dir[idx]) {
                go_to(s_entries[idx]);
                return;
            } else {
                preview_file(s_entries[idx]);
            }
        }
        s_selected = idx;
        if (!s_is_dir[idx]) preview_file(s_entries[idx]);
        s_dirty = true;
    }
}

void gui_filemanager_close(int wid, void*) {
    wm_destroy(wid);
    s_wid = -1;
}

//  Open 
void gui_filemanager_open() {
    if (s_wid >= 0) { wm_show(s_wid); wm_focus(s_wid); return; }

    static bool initialized = false;
    
    // If opening for the very first time, grab the current directory.
    // Otherwise, restore the directory we were looking at when we closed it.
    if (!initialized) {
        s_hist_idx = 0;
        s_hist_max = 0;
        vfs_getcwd(s_history[0], VFS_MAX_PATH);
        initialized = true;
    } else {
        // Restore the filesystem state to the file manager's last known path
        // (in case the shell changed the global cwd while we were closed!)
        vfs_chdir(s_history[s_hist_idx]);
    }

    refresh();

    s_wid = wm_create("File Manager", 100, 80, 720, 480);
    wm_set_callbacks(s_wid,
                     gui_filemanager_draw,
                     gui_filemanager_key,
                     gui_filemanager_close,
                     gui_filemanager_mouse,
                     nullptr);
}