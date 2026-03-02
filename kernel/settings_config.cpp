#include "../include/settings_config.h"
#include "../include/vfs.h"
#include "../include/kstring.h"

//  Global instance (defaults match original NodOS look) 
NodSettings g_settings = {
    /* wp_top    */ 0x1E3A5F,
    /* wp_bottom */ 0x0D1B2A,
    /* wp_name   */ "Classic",
    /* term_bg   */ 0x0A0F1A,
    /* term_fg   */ 0x00FF88,
    /* term_cursor*/ 0x00FF88,
    /* hostname  */ "nodos"
};

//  Helpers 

// Write val as exactly 6 uppercase hex digits into out (must be ≥ 7 bytes).
static void fmt_hex6(uint32_t val, char* out) {
    static const char H[] = "0123456789ABCDEF";
    out[0] = H[(val >> 20) & 0xF];
    out[1] = H[(val >> 16) & 0xF];
    out[2] = H[(val >> 12) & 0xF];
    out[3] = H[(val >>  8) & 0xF];
    out[4] = H[(val >>  4) & 0xF];
    out[5] = H[(val      ) & 0xF];
    out[6] = '\0';
}

// Parse a hex string (upper or lower, no prefix) → uint32_t.
static uint32_t parse_hex(const char* s) {
    uint32_t v = 0;
    while (*s) {
        char c = *s++;
        uint32_t d;
        if      (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else break;
        v = (v << 4) | d;
    }
    return v;
}

// Append "KEY=VALUE\n" to buf (buf must have room).
static void append_kv(char* buf, const char* key, const char* value) {
    k_strcat(buf, key);
    k_strcat(buf, "=");
    k_strcat(buf, value);
    k_strcat(buf, "\n");
}

//  Public API 

void settings_init() {
    // Defaults already set via the initialiser above.
    // Tries to load /settings.cfg; silently keeps defaults if absent.
    settings_load();   // silently does nothing if file absent
}

void settings_save() {
    static char buf[512];
    buf[0] = '\0';

    char hex[8];

    fmt_hex6(g_settings.wp_top,    hex); append_kv(buf, "WP_TOP",    hex);
    fmt_hex6(g_settings.wp_bottom, hex); append_kv(buf, "WP_BOT",    hex);
    append_kv(buf, "WP_NAME",  g_settings.wp_name);

    fmt_hex6(g_settings.term_bg,     hex); append_kv(buf, "TERM_BG",     hex);
    fmt_hex6(g_settings.term_fg,     hex); append_kv(buf, "TERM_FG",     hex);
    fmt_hex6(g_settings.term_cursor, hex); append_kv(buf, "TERM_CURSOR", hex);

    append_kv(buf, "HOSTNAME", g_settings.hostname);

    vfs_write("/settings.cfg", buf, k_strlen(buf));
}

void settings_load() {
    static char buf[512];
    if (vfs_read("/settings.cfg", buf, sizeof(buf) - 1) < 0) return;
    buf[511] = '\0';

    // Walk each line: KEY=VALUE
    char* line = buf;
    while (*line) {
        // Find end of line
        char* end = line;
        while (*end && *end != '\n') end++;
        char saved = *end;
        *end = '\0';

        // Split on '='
        char* eq = (char*)k_strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char* key = line;
            const char* val = eq + 1;

            if (k_strcmp(key, "WP_TOP")    == 0) g_settings.wp_top    = parse_hex(val);
            if (k_strcmp(key, "WP_BOT")    == 0) g_settings.wp_bottom = parse_hex(val);
            if (k_strcmp(key, "WP_NAME")   == 0) k_strncpy(g_settings.wp_name, val, 63);
            if (k_strcmp(key, "TERM_BG")   == 0) g_settings.term_bg   = parse_hex(val);
            if (k_strcmp(key, "TERM_FG")   == 0) g_settings.term_fg   = parse_hex(val);
            if (k_strcmp(key, "TERM_CURSOR")== 0) g_settings.term_cursor = parse_hex(val);
            if (k_strcmp(key, "HOSTNAME")  == 0) k_strncpy(g_settings.hostname, val, 31);
        }

        *end = saved;
        line = (*end) ? end + 1 : end;
    }
}