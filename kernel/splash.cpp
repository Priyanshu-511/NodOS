// splash.cpp — NodOS Boot Splash Screen
//
// "NodOS" is drawn from hand-coded 5×9 pixel-art bitmaps, each pixel
// rendered as a large filled rectangle — this is the only reliable way
// to get a truly big logo when fb_draw_char is locked to 8×8 glyphs.
//
// Visual sequence:
//   1. Deep-space starfield
//   2. CRT power-on glitch burst
//   3. "NodOS" pixel-art logo slams in from above, neon-glow halos
//   4. Tagline + version badge
//   5. Animated loading steps (ASCII spinner, ellipsis)
//   6. Segmented progress bar with leading-edge glow
//   7. Quadratic-ease iris-wipe fade to desktop

#include "../include/splash.h"
#include "../include/fb.h"
#include "../include/pit.h"
#include "../include/kstring.h"

static constexpr int W = (int)FB_WIDTH;
static constexpr int H = (int)FB_HEIGHT;


//  Timing


static void delay_ms(uint32_t ms) {
    uint32_t target = pit_uptime_ms() + ms;
    while (pit_uptime_ms() < target)
        __asm__ volatile("hlt");
}


//  xorshift32 RNG


static uint32_t s_rng = 0xA3C1E597;
static uint32_t rng_next() {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}


//  Colour helpers


static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint32_t col_lerp(uint32_t a, uint32_t b, int t, int max = 16) {
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    return rgb((uint8_t)(ar + (br - ar) * t / max),
               (uint8_t)(ag + (bg - ag) * t / max),
               (uint8_t)(ab + (bb - ab) * t / max));
}


//  Percentage string (no leading zeros, no libc)


static const char* fmt_pct(char* buf, int v) {
    if (v < 0) v = 0; if (v > 100) v = 100;
    char tmp[4]; int n = 0, t = v;
    if (!t) tmp[n++] = '0';
    else while (t) { tmp[n++] = '0' + t % 10; t /= 10; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '%'; buf[n + 1] = '\0';
    return buf;
}


//  Starfield


struct Star { int16_t x, y; uint32_t col; };
static Star s_stars[300];
static bool s_stars_ready = false;

static void build_stars() {
    if (s_stars_ready) return;
    s_rng = 0xA3C1E597;
    for (int i = 0;   i < 200; i++)
        s_stars[i] = { (int16_t)(rng_next() % W), (int16_t)(rng_next() % H), 0x0C1C2C };
    for (int i = 200; i < 265; i++)
        s_stars[i] = { (int16_t)(rng_next() % W), (int16_t)(rng_next() % H), 0x1E3850 };
    for (int i = 265; i < 300; i++)
        s_stars[i] = { (int16_t)(rng_next() % W), (int16_t)(rng_next() % H), 0x557799 };
    s_stars_ready = true;
}

static void draw_background() {
    fb_fill_gradient(0, 0, W, H, 0x00020A, 0x010C1C);
    // Faint nebula band
    fb_fill_gradient(0, H/3 - 28, W, 28, 0x000000, 0x00060F);
    fb_fill_gradient(0, H/3,      W, 28, 0x00060F, 0x000000);
    build_stars();
    for (int i = 0; i < 300; i++)
        fb_fill_rect(s_stars[i].x, s_stars[i].y, 1, 1, s_stars[i].col);
}


//  CRT glitch


static void crt_glitch() {
    for (int f = 0; f < 10; f++) {
        draw_background();
        int n = 3 + f / 2;
        for (int l = 0; l < n; l++) {
            int ly = (int)(rng_next() % (uint32_t)H);
            uint32_t c = (f < 5) ? 0x004466 : 0x001A2A;
            fb_fill_rect(0, ly, W, 1 + (int)(rng_next() % 2), c);
        }
        fb_swap();
        delay_ms(28);
    }
}


//  PIXEL-ART LOGO — "NodOS"
//
//  Each letter is a 5-column × 9-row bitmap.
//  Row data: bit 4 = leftmost column, bit 0 = rightmost.
//  Each lit pixel is drawn as a PIXEL_S × PIXEL_S filled rectangle,
//  giving real, large, fully visible lettering.
//
//  PIXEL_S=8  → each letter ≈ 40 × 72 px   (very large, unmissable)


static constexpr int PIXEL_S   = 8;   // size of one "pixel" block
static constexpr int LETTER_W  = 5;   // columns per glyph
static constexpr int LETTER_H  = 9;   // rows per glyph
static constexpr int LETTER_PW = LETTER_W * PIXEL_S;  // 40 px
static constexpr int LETTER_PH = LETTER_H * PIXEL_S;  // 72 px
static constexpr int LETTER_GAP = 12;
static constexpr int LOGO_N    = 5;

// Bitmaps for N, o, d, O, S  (9 rows × 5 bits)
static const uint8_t GLYPH_N[LETTER_H] = {
    0b10001,
    0b11001,
    0b11001,
    0b10101,
    0b10011,
    0b10011,
    0b10001,
    0b10001,
    0b10001,
};
static const uint8_t GLYPH_o[LETTER_H] = {
    0b00000,
    0b00000,
    0b01110,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b01110,
};
static const uint8_t GLYPH_d[LETTER_H] = {
    0b00001,
    0b00001,
    0b01101,
    0b10011,
    0b10001,
    0b10001,
    0b10011,
    0b01101,  // was 01101 — asymmetric on purpose for 'd' feel
    0b00000,
};
static const uint8_t GLYPH_O[LETTER_H] = {
    0b01110,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b01110,
};
static const uint8_t GLYPH_S[LETTER_H] = {
    0b01111,
    0b10000,
    0b10000,
    0b10000,
    0b01110,
    0b00001,
    0b00001,
    0b00001,
    0b11110,
};

static const uint8_t* GLYPHS[LOGO_N] = {
    GLYPH_N, GLYPH_o, GLYPH_d, GLYPH_O, GLYPH_S
};

// Neon colour per letter: core, inner halo, outer halo
static const uint32_t LOGO_CORE [LOGO_N] = {
    0x00FFFF, 0x33EEFF, 0x55DDFF, 0x00FFFF, 0x33EEFF
};
static const uint32_t LOGO_HALO1[LOGO_N] = {   // 1-px fringe  (dim teal)
    0x007799, 0x006688, 0x005577, 0x007799, 0x006688
};
static const uint32_t LOGO_HALO2[LOGO_N] = {   // 2-px fringe  (very dim)
    0x001E2A, 0x001422, 0x001022, 0x001E2A, 0x001422
};

static constexpr int LOGO_TOTAL_W =
    LOGO_N * LETTER_PW + (LOGO_N - 1) * LETTER_GAP;

// Draw a single pixel-art glyph with concentric glow halos.
// cx, cy = top-left of the glyph's pixel grid.
static void draw_glyph(int cx, int cy, int gi) {
    const uint8_t* bmp   = GLYPHS[gi];
    const uint32_t core  = LOGO_CORE [gi];
    const uint32_t halo1 = LOGO_HALO1[gi];
    const uint32_t halo2 = LOGO_HALO2[gi];

    for (int row = 0; row < LETTER_H; row++) {
        for (int col = 0; col < LETTER_W; col++) {
            if (!(bmp[row] & (1 << (LETTER_W - 1 - col)))) continue;

            int px = cx + col * PIXEL_S;
            int py = cy + row * PIXEL_S;

            // Outer halo: 1 px outside the block on each side
            fb_fill_rect(px - 2, py - 2, PIXEL_S + 4, PIXEL_S + 4, halo2);
            // Inner halo: tight fringe
            fb_fill_rect(px - 1, py - 1, PIXEL_S + 2, PIXEL_S + 2, halo1);
            // Drop shadow
            fb_fill_rect(px + 3, py + 3, PIXEL_S, PIXEL_S, 0x000A14);
            // Core pixel
            fb_fill_rect(px,     py,     PIXEL_S, PIXEL_S, core);
        }
    }
}

static void draw_logo(int top_y) {
    const int start_x = (W - LOGO_TOTAL_W) / 2;
    for (int i = 0; i < LOGO_N; i++) {
        int bx = start_x + i * (LETTER_PW + LETTER_GAP);
        draw_glyph(bx, top_y, i);
    }

    // Underline: dual gradient converging at centre
    const int gw = LOGO_TOTAL_W + 80;
    const int gx = (W - gw) / 2;
    const int gy = top_y + LETTER_PH + 10;
    fb_fill_gradient(gx,          gy,     gw / 2, 2, 0x000000, 0x00BBDD);
    fb_fill_gradient(gx + gw / 2, gy,     gw / 2, 2, 0x00BBDD, 0x000000);
    fb_fill_gradient(gx + 30,     gy - 1, gw / 2 - 30, 1, 0x000000, 0x004466);
    fb_fill_gradient(gx + gw / 2, gy - 1, gw / 2 - 30, 1, 0x004466, 0x000000);
}


//  Logo slam-in animation


static constexpr int LOGO_Y = 70;   // final resting y-position

static void animate_logo_in() {
    static const int OFF[] = { -H, -H/2, -H/5, -24, 16, -6, 2, 0 };
    for (int f = 0; f < 8; f++) {
        draw_background();
        draw_logo(LOGO_Y + OFF[f]);
        fb_swap();
        delay_ms(f < 4 ? 22 : 38);
    }
}


//  Centred text helper


static void draw_centered(int y, const char* txt, uint32_t col) {
    fb_draw_text((W - fb_text_width(txt)) / 2, y, txt, col);
}


//  Progress bar — segmented with leading-edge glow


static constexpr int BAR_W   = 500;
static constexpr int BAR_H   = 10;
static constexpr int BAR_X   = (W - BAR_W) / 2;
static constexpr int BAR_Y   = H - 52;
static constexpr int SEG_W   = 6;
static constexpr int SEG_GAP = 2;
static constexpr int SEG_TOT = SEG_W + SEG_GAP;

static void clear_bar_region() {
    fb_fill_gradient(0, BAR_Y - 10, W, BAR_H + 24, 0x00020A, 0x010C1C);
}

static void draw_progress(int pct) {
    clear_bar_region();
    fb_draw_rect(BAR_X - 2, BAR_Y - 2, BAR_W + 4, BAR_H + 4, 0x071C2E);
    fb_draw_rect(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2, 0x0A2F4A);
    fb_fill_rect(BAR_X,     BAR_Y,     BAR_W,     BAR_H,     0x020B14);

    const int filled_px = (BAR_W * pct) / 100;
    const int edge_x    = BAR_X + filled_px;

    for (int sx = BAR_X; sx + SEG_W <= BAR_X + BAR_W; sx += SEG_TOT) {
        if (sx + SEG_W <= edge_x) {
            int dist = edge_x - sx;
            uint32_t c;
            if      (dist <= SEG_TOT)      c = 0x00FFFF;
            else if (dist <= SEG_TOT * 3)  c = 0x0099CC;
            else if (dist <= SEG_TOT * 7)  c = 0x006699;
            else                           c = 0x003D5C;
            fb_fill_rect(sx, BAR_Y, SEG_W, BAR_H, c);
        } else {
            fb_fill_rect(sx, BAR_Y, SEG_W, BAR_H, 0x040F1A);
        }
    }

    char buf[6];
    fb_draw_text(BAR_X + BAR_W + 12, BAR_Y + 1, fmt_pct(buf, pct), 0x2A7A9A);
}


//  Loading steps
//  NOTE: Only pure ASCII glyphs used — no cp437 extended chars.
//        Spinner: - \ | /     Checkmark: +     Dots: . .. ...


struct LoadStep { const char* label; int hold_ms; int progress; };

static const LoadStep STEPS[] = {
    { "Initializing memory manager",      100,  12 },
    { "Loading filesystem driver",         130,  26 },
    { "Starting process scheduler",         90,  39 },
    { "Mounting virtual filesystems",      110,  52 },
    { "Initializing input devices",        120,  65 },
    { "Loading desktop environment",       180,  79 },
    { "Applying theme and settings",       130,  92 },
    { "Ready",                              40, 100 },
};
static constexpr int STEP_COUNT  = 8;
static constexpr int STEP_LINE_H = 12;

static const char* SPINNER[4] = { "-", "\\", "|", "/" };
static const char* DOTS[4]    = { "   ", ".  ", ".. ", "..." };

static void draw_steps(int base_y, int current) {
    const int area_h = STEP_COUNT * STEP_LINE_H + 16;
    fb_fill_gradient(0, base_y - 4, W, area_h, 0x00020A, 0x010C1C);

    int max_w = 0;
    for (int i = 0; i < STEP_COUNT; i++) {
        int w = fb_text_width(STEPS[i].label);
        if (w > max_w) max_w = w;
    }
    const int TICK_W  = 10;
    const int block_x = (W - TICK_W - max_w) / 2;

    int y = base_y + 2;
    for (int i = 0; i < STEP_COUNT && i <= current; i++) {
        if (i < current) {
            fb_draw_text(block_x,          y, "+", 0x00BB55);
            fb_draw_text(block_x + TICK_W, y, STEPS[i].label, 0x0F3525);
        } else {
            int sp = (int)((pit_uptime_ms() / 120) % 4);
            fb_draw_text(block_x,          y, SPINNER[sp], 0x00DDAA);
            fb_draw_text(block_x + TICK_W, y, STEPS[i].label, 0x00FFCC);
            int dp = (int)((pit_uptime_ms() / 220) % 4);
            int tx = block_x + TICK_W + fb_text_width(STEPS[i].label) + 2;
            fb_draw_text(tx, y, DOTS[dp], 0x005544);
        }
        y += STEP_LINE_H;
    }
}


//  Iris-wipe fade to desktop


static void fade_to_desktop() {
    const uint32_t D = Color::Desktop;
    for (int f = 0; f <= 30; f++) {
        int ef = f * f / 30;
        int hm = (H / 2) * ef / 30;
        int wm = (W / 2) * ef / 30;
        fb_fill_rect(0,      0,      W,  hm, D);
        fb_fill_rect(0,      H - hm, W,  hm, D);
        fb_fill_rect(0,      0,      wm, H,  D);
        fb_fill_rect(W - wm, 0,      wm, H,  D);
        fb_swap();
        delay_ms(10);
    }
    fb_clear(D); fb_swap();
}


//  Entry point


void splash_show() {
    // CRT glitch
    crt_glitch();

    // Starfield
    draw_background();
    fb_swap();
    delay_ms(80);

    // Logo slams in
    animate_logo_in();
    delay_ms(180);

    // Tagline
    const int TAGLINE_Y = LOGO_Y + LETTER_PH + 26;
    draw_centered(TAGLINE_Y,
                  "A hobby operating system, built from scratch",
                  0x18445A);

    // Version badge — pure ASCII only
    const int VER_Y = TAGLINE_Y + 14;
    {
        const char* ver = "v4.0  |  x86 32-bit  |  1024x768";
        int vw = fb_text_width(ver) + 20;
        int vx = (W - vw) / 2;
        fb_draw_rect (vx,     VER_Y - 2, vw,    11, 0x0A2030);
        fb_fill_rect (vx + 1, VER_Y - 1, vw-2,   9, 0x030E18);
        draw_centered(VER_Y, ver, 0x1A5A7A);
    }

    fb_swap();
    delay_ms(180);

    // Loading steps + bar
    const int STEP_BASE_Y = VER_Y + 22;
    draw_progress(0);
    fb_swap();
    delay_ms(180);

    for (int i = 0; i < STEP_COUNT; i++) {
        const LoadStep& s   = STEPS[i];
        int from_pct = (i == 0) ? 0 : STEPS[i-1].progress;
        int to_pct   = s.progress;

        // Animate bar fill
        for (int p = from_pct; p <= to_pct; p++) {
            draw_background();
            draw_logo(LOGO_Y);
            draw_centered(TAGLINE_Y,
                          "A hobby operating system, built from scratch",
                          0x18445A);
            {
                const char* ver = "v4.0  |  x86 32-bit  |  1024x768";
                int vw = fb_text_width(ver) + 20;
                int vx = (W - vw) / 2;
                fb_draw_rect(vx, VER_Y-2, vw, 11, 0x0A2030);
                fb_fill_rect(vx+1, VER_Y-1, vw-2, 9, 0x030E18);
                draw_centered(VER_Y, ver, 0x1A5A7A);
            }
            draw_steps(STEP_BASE_Y, i);
            draw_progress(p);
            fb_swap();
            delay_ms(6);
        }

        // Hold at this step
        uint32_t hold_end = pit_uptime_ms() + (uint32_t)s.hold_ms;
        while (pit_uptime_ms() < hold_end) {
            draw_background();
            draw_logo(LOGO_Y);
            draw_centered(TAGLINE_Y,
                          "A hobby operating system, built from scratch",
                          0x18445A);
            {
                const char* ver = "v4.0  |  x86 32-bit  |  1024x768";
                int vw = fb_text_width(ver) + 20;
                int vx = (W - vw) / 2;
                fb_draw_rect(vx, VER_Y-2, vw, 11, 0x0A2030);
                fb_fill_rect(vx+1, VER_Y-1, vw-2, 9, 0x030E18);
                draw_centered(VER_Y, ver, 0x1A5A7A);
            }
            draw_steps(STEP_BASE_Y, i);
            draw_progress(to_pct);
            fb_swap();
            delay_ms(28);
        }
    }

    // Final frame
    draw_background();
    draw_logo(LOGO_Y);
    draw_steps(STEP_BASE_Y, STEP_COUNT);
    draw_progress(100);
    fb_swap();
    delay_ms(420);

    fade_to_desktop();
}