/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM-1 lo-fi sampler: panel, screen, MIDI input and the BLE/USB SysEx pack upload.
 * Replaces the stock UI (app/screen.c). The sound is made by lofi_engine.c (called from the audio interrupt, audio.c).
 * Protocol: core/lofi_proto.h (also in this directory). */
#include <string.h>
#include "fm1.h"
#include "fm1_device.h"
#include "instrument.h"
#define LOFI_DECODE_ONLY
#include "lofi_engine.h"
#include "lofi_proto.h"

enum { KEYS = 27, BUTTONS = 14, ENC = 7, QN = 40, MIDIQ = 32 };
enum { BTN_OCT_DOWN = 0, BTN_OCT_UP = 1, BTN_PLAY = 12, BTN_REC = 13 };
static const uint16_t zone_col[12] = {0xfd20, 0x07ff, 0x07e0, 0xf81f, 0xffe0, 0x041f, 0xf800, 0x899f, 0xafe5, 0xfc18, 0x0455, 0xef7d};
enum { COL_BG = 0x0000, COL_DIM = 0x4208, COL_FG = 0xef7d, COL_ACC = 0xfd20, COL_OK = 0x07e0, COL_BAR = 0x2104 };

/* ---- drawing ------------------------------------------------------------- */
/* Five columns per glyph, bit 0 at the top. 0-9, A-Z, then punctuation. */
static const uint8_t font[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},       {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};
static const uint8_t punct[][6] = {   /* char, 5 columns */
    {'.', 0, 0x60, 0x60, 0, 0},      {':', 0, 0x36, 0x36, 0, 0},      {'-', 8, 8, 8, 8, 8},
    {'/', 0x20, 0x10, 0x08, 0x04, 0x02}, {'%', 0x23, 0x13, 0x08, 0x64, 0x62}, {'+', 8, 8, 0x3e, 8, 8},
    {'>', 0, 0x41, 0x22, 0x14, 8},   {'<', 8, 0x14, 0x22, 0x41, 0}, {'_', 0x40, 0x40, 0x40, 0x40, 0x40},
    {'!', 0, 0, 0x5f, 0, 0},         {'?', 2, 1, 0x51, 9, 6},        {'#', 0x14, 0x7f, 0x14, 0x7f, 0x14},
};

static const uint8_t *glyph(unsigned c)
{
    static const uint8_t blank[5];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= '0' && c <= '9') return font[c - '0'];
    if (c >= 'A' && c <= 'Z') return font[10 + c - 'A'];
    for (unsigned i = 0; i < sizeof punct / sizeof punct[0]; ++i)
        if (punct[i][0] == c) return punct[i] + 1;
    return blank;
}

/* ---- the screen is driven through a queue: drawing code only enqueues operations (cheap, no SPI), and every control tick flushes a limited amount.
 * A page redraw used to block the control task for tens of milliseconds (hundreds of calls, 90k pixels): the keys were polled late. ---- */
enum { GQ = 768, POOL_PX = 12288, BAND_PX = 3000 };
typedef struct { uint16_t x, y, w, h, c, off; uint8_t blit; } gop_t;
static gop_t gq[GQ]; static uint16_t pool[POOL_PX]; static unsigned g_head, g_tail, pool_w;
static void gfx_exec(const gop_t *o) { if (o->blit) (void)fm1_screen_blit(o->x, o->y, o->w, o->h, pool + o->off, (size_t)o->w * o->h); else fm1_screen_fill(o->x, o->y, o->w, o->h, o->c); }
static void gfx_flush(unsigned budget)       /* cost of an operation: 4 + pixels / 64 */
{
    unsigned used = 0;
    while (g_head != g_tail && used < budget) { const gop_t *o = &gq[g_head % GQ]; gfx_exec(o); used += 4 + (unsigned)o->w * o->h / 64; ++g_head; }
    if (g_head == g_tail) pool_w = 0;
}
static void gfx_reset(void) { g_head = g_tail = 0; pool_w = 0; }
static int gfx_idle(void) { return g_head == g_tail; }
static gop_t *gfx_slot(void) { if (g_tail - g_head >= GQ) gfx_flush(~0u); gop_t *o = &gq[g_tail % GQ]; ++g_tail; return o; }
static void gfx_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t c)
{
    if (x >= 240 || y >= 240 || !w || !h) return; if (x + w > 240) w = 240 - x; if (y + h > 240) h = 240 - y;
    unsigned rows = BAND_PX / w; if (!rows) rows = 1;
    for (unsigned r = 0; r < h; r += rows) { unsigned hh = h - r < rows ? h - r : rows; gop_t *o = gfx_slot(); o->x = (uint16_t)x; o->y = (uint16_t)(y + r); o->w = (uint16_t)w; o->h = (uint16_t)hh; o->c = c; o->blit = 0; }
}
static uint16_t *gfx_pixels(unsigned x, unsigned y, unsigned w, unsigned h)     /* queue a blit and return its pixel buffer */
{
    unsigned n = w * h; if (pool_w + n > POOL_PX) { gfx_flush(~0u); pool_w = 0; }
    gop_t *o = gfx_slot(); o->x = (uint16_t)x; o->y = (uint16_t)y; o->w = (uint16_t)w; o->h = (uint16_t)h; o->blit = 1; o->off = (uint16_t)pool_w; pool_w += n; return pool + o->off;
}
static void draw_text(unsigned x, unsigned y, unsigned scale, const char *t, uint16_t fg, uint16_t bg)
{
    unsigned n = 0, w, h = 8 * scale; while (t[n]) ++n;
    if (!n || x >= 240 || y + h > 240) return; if (x + n * 6 * scale > 240) n = (240 - x) / (6 * scale); if (!n) return;
    w = n * 6 * scale; if (w * h > POOL_PX) return;
    uint16_t *buf = gfx_pixels(x, y, w, h);
    for (unsigned ci = 0; ci < n; ++ci) {
        const uint8_t *g = glyph((unsigned char)t[ci]);
        for (unsigned py = 0; py < h; ++py) { unsigned row = py / scale; for (unsigned px = 0; px < 6 * scale; ++px) { unsigned col = px / scale; buf[py * w + ci * 6 * scale + px] = col < 5 && row < 7 && (g[col] >> row & 1U) ? fg : bg; } }
    }
}
static void draw_pad(unsigned x, unsigned y, unsigned scale, const char *t, unsigned width_chars, uint16_t fg, uint16_t bg)
{
    char b[32]; unsigned n = 0;
    while (t[n] && n < width_chars && n < 31) { b[n] = t[n]; ++n; }
    while (n < width_chars && n < 31) b[n++] = ' ';
    b[n] = 0; draw_text(x, y, scale, t ? b : "", fg, bg);
}
static unsigned fmt_int(char *o, int v)   /* returns length */
{
    char t[12]; unsigned n = 0, k = 0; unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) o[k++] = '-';
    while (n) o[k++] = t[--n];
    o[k] = 0; return k;
}

/* ---- SysEx receive: routing task -> control task ------------------------------------------------------------ */
static struct { uint8_t data[LF_MAXMSG]; uint8_t source; volatile uint16_t length; } rxq[QN];
static volatile uint8_t rx_head; static uint8_t rx_tail;
static volatile uint32_t rx_frames, rx_dropped;
static void barrier(void)
{
#ifndef FM1_HOST_TEST
    __asm__ volatile("csync" ::: "memory");
#endif
}
/* Called from fm1_app_sysex (transfer.c). */
int lofi_sysex(const uint8_t *m, size_t n)
{
    if (n < 10 || m[0] != 0xf0 || m[1] != 0x7d || m[2] != 'L' || m[3] != 'F') return 0;
    rx_frames++;
    uint8_t slot = rx_head;
    if (n > LF_MAXMSG || rxq[slot].length) { rx_dropped++; return 1; }
    memcpy(rxq[slot].data, m, n); rxq[slot].source = (uint8_t)fm1_midi_input_source();
    barrier(); rxq[slot].length = (uint16_t)n; rx_head = (uint8_t)((slot + 1) % QN);
    fm1_control_request();
    return 1;
}
/* ---- MIDI input: routing task -> control task ----------------------------------------------------------------- */
static struct { uint8_t m[3]; } midiq[MIDIQ]; static volatile uint8_t mq_w; static uint8_t mq_r;
void lofi_midi(const uint8_t *m, size_t n)
{
    unsigned st = m[0] & 0xf0U;
    if (n < 2 || (st != 0x80 && st != 0x90 && st != 0xe0 && st != 0xb0)) return;
    uint8_t w = mq_w, nx = (uint8_t)((w + 1) % MIDIQ); if (nx == mq_r) return;
    midiq[w].m[0] = m[0]; midiq[w].m[1] = m[1]; midiq[w].m[2] = n > 2 ? m[2] : 0; barrier(); mq_w = nx;
    fm1_control_request();
}

/* ---- flash glue for the upload protocol ------------------------------------------------------------------------ */
static int fl_read(void *c, uint32_t o, void *p, size_t n) { (void)c; return fm1_flash_read(o, p, n); }
static int fl_write(void *c, uint32_t o, const void *p, size_t n) { (void)c; return fm1_flash_write(o, p, n); }
static int fl_erase(void *c, uint32_t o) { (void)c; return fm1_flash_erase(o); }
static lf_dev_t dev;
static uint8_t flash_ready, reply_len, reply_port; static uint8_t reply_buf[LF_MAXMSG];
static uint32_t upload_seen; static uint8_t upload_active;

/* ---- state --------------------------------------------------------------------------------------------------------- */
enum { PG_HOME, PG_FX, PG_SEL, PG_ENV, PG_LFO, PG_EDIT, PG_GLO, PG_SAVE, PG_ARP, PG_SEQ, PG_COUNT };
static const uint8_t page_btn[PG_COUNT] = {8, 2, 3, 4, 5, 6, 7, 9, 10, 11};      /* panel buttons: HOME FX SEL ENV LFO EDIT GLO SAVE ARP SEQ */
static const char *const page_name[PG_COUNT] = {"HOME", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "SAVE", "ARP", "SEQ"};
static const char *const fx_name[FX_COUNT] = {"REVERB", "DELAY", "CRUSH", "FILTER"};
static const char *const punch_name[PN_COUNT] = {"REV", "DLY", "CRU", "DWN", "LPF", "HPF", "ST8", "S16", "GAT", "HLF", "DBL", "STP"};
static const char *const note_nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static uint8_t page = PG_HOME, dirty = 3, bound_seen, unsaved, fx_sel, hdr_key_valid; static uint32_t hdr_key;
static int az;                                                 /* the active zone: ENV, LFO, EDIT, FX sends and the waveform follow it */
static uint8_t bank;     /* the zone of 27 keys the panel plays: OCT- / OCT+ switch it; note id = bank * 32 + key */
static uint32_t down_keys, last_draw, upload_draw, msg_until; static uint64_t led_have; static uint8_t led_force = 1;
static uint32_t sounding;    /* keys of the current zone that sound right now (held, sequencer, arpeggiator, MIDI) */
static uint8_t key_act[KEYS]; static uint8_t held_note[KEYS]; static const char *msg_text = "";
static int8_t wmin[220], wmax[220]; static int wave_slot = -1; static uint32_t wave_sb; static uint8_t wave_ready;

static unsigned nz(void) { const lfp_info_t *i = le_info(); return i ? i->n_slots : 0; }
static unsigned nbanks(void) { const lfp_info_t *i = le_info(); unsigned n = 1; if (i) for (unsigned k = 0; k < i->n_slots; ++k) if ((unsigned)i->slot[k].lo / 32 + 1 > n) n = (unsigned)i->slot[k].lo / 32 + 1; return n > 8 ? 8 : n; }
static int in_bank(unsigned slot) { const lfp_info_t *i = le_info(); return i && slot < i->n_slots && i->slot[slot].lo / 32 == bank; }
static int melodic_slot(void) { const lfp_info_t *i = le_info(); if (i) for (unsigned k = 0; k < i->n_slots; ++k) if (in_bank(k) && i->slot[k].hi > i->slot[k].lo) return (int)k; return -1; }   /* the melodic sample of the bank, or -1 for a kit */
static int first_in_bank(void) { for (unsigned k = 0; k < nz(); ++k) if (in_bank(k)) return (int)k; return -1; }
static void step_az(int dir) { int n = (int)nz(), i = az; for (int t = 0; t < n; ++t) { i += dir; if (i < 0 || i >= n) return; if (in_bank(i)) { az = i; return; } } }
static void set_bank(int b) { unsigned nb = nbanks(); if (b < 0) b = 0; if (b >= (int)nb) b = (int)nb - 1; if ((int)bank != b) { bank = (uint8_t)b; int f = first_in_bank(); if (f >= 0) az = f; } }
static int bump(int v, int d, int lo, int hi) { v += d; return v < lo ? lo : v > hi ? hi : v; }
static void say(const char *t, uint32_t now) { msg_text = t; msg_until = now + 1800U; dirty |= 2; }

/* ---- persistence: the last two sectors of the partition keep the loops and the settings of the pack: 'LFPS', pack crc, lengths, crc, payload ---- */
static uint8_t tail_buf[2 * LF_SECTOR];
static void tail_load(void)
{
    uint32_t at = fm1_flash_size() / LF_SECTOR - 2;
    if (fm1_flash_read(at * LF_SECTOR, tail_buf, sizeof tail_buf)) return;
    if (lf_g32(tail_buf) != 0x53504c46u || lf_g32(tail_buf + 4) != dev.pack_crc) return;     /* 'LFPS', and only for the pack they were made for */
    unsigned a = tail_buf[8] | tail_buf[9] << 8, b = tail_buf[10] | tail_buf[11] << 8;
    if (a + b > sizeof tail_buf - 16 || lf_g32(tail_buf + 12) != lf_crc32(0, tail_buf + 16, a + b)) return;
    if (a) (void)le_seq_import(tail_buf + 16, a);
    if (b) (void)le_settings_import(tail_buf + 16 + a, b);
    unsaved = 0;
}
static int tail_save(void)
{
    struct le_seq_status st; le_seq_status_get(&st); if (st.state == LS_REC || st.state == LS_COUNTIN || st.state == LS_FREE) return -1;
    memset(tail_buf, 0xff, sizeof tail_buf);
    unsigned a = le_seq_export(tail_buf + 16, 7000), b = le_settings_export(tail_buf + 16 + a, sizeof tail_buf - 16 - a);
    lf_p32(tail_buf, 0x53504c46u); lf_p32(tail_buf + 4, dev.pack_crc); tail_buf[8] = (uint8_t)a; tail_buf[9] = (uint8_t)(a >> 8); tail_buf[10] = (uint8_t)b; tail_buf[11] = (uint8_t)(b >> 8);
    lf_p32(tail_buf + 12, lf_crc32(0, tail_buf + 16, a + b));
    uint32_t off = (fm1_flash_size() / LF_SECTOR - 2) * LF_SECTOR;
    for (unsigned s = 0; s < 2; ++s) if (fm1_flash_erase(off + s * LF_SECTOR) || fm1_flash_write(off + s * LF_SECTOR, tail_buf + s * LF_SECTOR, LF_SECTOR)) return -2;
    unsaved = 0; return 0;
}
static int bind_pack(void)
{
    if (dev.state != LF_PACK) { (void)le_bind(NULL, 0); return -1; }
    const uint8_t *p = fm1_flash_map(2 * LF_SECTOR, dev.pack_size);
    if (!p || le_bind(p, dev.pack_size)) return -1;
    unsaved = 0; tail_load(); bank = 0; { int f = first_in_bank(); az = f < 0 ? 0 : f; } wave_slot = -1;
    return 0;
}

/* ---- waveform of the active zone, analysed a few superblocks per tick ---- */
static void wave_start(int slot) { wave_slot = slot; wave_sb = 0; wave_ready = 0; memset(wmin, 0, sizeof wmin); memset(wmax, 0, sizeof wmax); }
static void wave_step(void)
{
    const lfp_info_t *in = le_info(); const uint8_t *pk = le_pack();
    if (!in || !pk || az < 0 || (unsigned)az >= in->n_slots) return;
    if (wave_slot != az) wave_start(az);
    if (wave_ready) return;
    const lfp_slot_t *s = &in->slot[wave_slot]; unsigned nsb = (s->frames + LOFI_SUPER - 1) / LOFI_SUPER;
    for (unsigned n = 0; n < 2 && wave_sb < nsb; n++, wave_sb++) {
        int16_t pcm[LOFI_SUPER]; lofi_decode_super(pk + s->offset + wave_sb * LOFI_SUPER_BYTES, pcm);
        for (unsigned i = 0; i < LOFI_SUPER; i++) {
            uint32_t fi = wave_sb * LOFI_SUPER + i; if (fi >= s->frames) break;
            unsigned col = (unsigned)((uint64_t)fi * 220 / s->frames); int v = pcm[i] / 256;
            if (v < wmin[col]) wmin[col] = (int8_t)v; if (v > wmax[col]) wmax[col] = (int8_t)v;
        }
    }
    if (wave_sb >= nsb) { wave_ready = 1; dirty |= 2; }
}

/* ---- drawing helpers ---- */
static void line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1, dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) { gfx_fill((unsigned)x0, (unsigned)y0, 2, 2, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
}
static unsigned fmt_ms(char *o, unsigned ms)
{
    unsigned n;
    if (ms >= 1000) { n = (unsigned)fmt_int(o, (int)(ms / 1000)); o[n++] = '.'; o[n++] = (char)('0' + (ms % 1000) / 100); o[n++] = 'S'; } else { n = (unsigned)fmt_int(o, (int)ms); o[n++] = 'M'; o[n++] = 'S'; }
    o[n] = 0; return n;
}
static unsigned ui_ms(unsigned idx) { static const uint16_t b[4] = {1000, 1189, 1414, 1682}; unsigned p = idx * 48 / 127; return ((unsigned)b[p & 3] << (p >> 2)) / 1000; }   /* the engine's 1 ms .. 4 s */
static unsigned put(char *o, unsigned n, const char *s) { while (*s) o[n++] = *s++; o[n] = 0; return n; }
static int zone_of_note(const lfp_info_t *info, int note)
{
    if (!info || note < 0) return -1;
    for (unsigned i = 0; i < info->n_slots; i++) if (note >= info->slot[i].lo && note <= info->slot[i].hi) return (int)i;
    return -1;
}
static void draw_strip_key(const lfp_info_t *info, unsigned y0, unsigned k);
/* the 27 keys as a strip coloured by zone; muted zones are dim, keys held right now tall and white */
static void draw_strip_key(const lfp_info_t *info, unsigned y0, unsigned k)
{
    enum { X0 = 12, W = 8, H = 20 };
    int note = (int)bank * 32 + (int)k, z = zone_of_note(info, note); unsigned pc = (k + 5) % 12, black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    uint16_t c = z < 0 ? COL_BAR : (le_zone((unsigned)z)->mute ? COL_DIM : zone_col[z % 12]); unsigned h = black ? H - 6 : H, y = y0 + (H - h);
    if ((down_keys | sounding) >> k & 1U) { c = 0xffff; y = y0 - 3; h = H + 3; }
    gfx_fill(X0 + k * W, y0 - 3, W, H + 3, COL_BG);
    gfx_fill(X0 + k * W + 1, y, W - 2, h, c);
}
static void draw_strip(const lfp_info_t *info, unsigned y0) { for (unsigned k = 0; k < KEYS; ++k) draw_strip_key(info, y0, k); }

/* ---- the four parameter tiles of every page ---- */
typedef struct { char name[9]; char val[12]; int frac; } tile_t;       /* frac 0..100, -1 = no bar */
static void tile(tile_t *t, const char *name, const char *val, int frac) { unsigned i = 0; while (name[i] && i < 8) { t->name[i] = name[i]; ++i; } t->name[i] = 0; i = 0; while (val[i] && i < 11) { t->val[i] = val[i]; ++i; } t->val[i] = 0; t->frac = frac; }
static void tile_int(tile_t *t, const char *name, int v, int lo, int hi) { char b[12]; fmt_int(b, v); tile(t, name, b, hi > lo ? (v - lo) * 100 / (hi - lo) : 0); }
static void page_tiles(unsigned pg, tile_t t[4])
{
    char b[16]; struct le_seq_status st; le_seq_status_get(&st); const lfp_info_t *in = le_info();
    le_zone_t *z = le_zone((unsigned)az); const lfp_slot_t *sl = in && (unsigned)az < in->n_slots ? &in->slot[az] : 0; unsigned n;
    static const char *const shape[4] = {"SINE", "TRI", "SQUARE", "SAW"}, *const target[3] = {"PITCH", "VOLUME", "PAN"}, *const dirn[4] = {"UP", "DOWN", "UP-DOWN", "RANDOM"};
    static const char *const ratn[4] = {"1/4", "1/8", "1/16", "1/32"}, *const qn[4] = {"OFF", "1/8", "1/16", "1/32"}, *const dly[8] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "3/4", "1 BEAT"};
    static const char *const metro[4] = {"OFF", "CLICK", "COUNT", "BOTH"};
    for (unsigned i = 0; i < 4; ++i) tile(&t[i], "", "", -1);
    switch (pg) {
    case PG_HOME: { int s = le_get_speed(), a = s < 0 ? -s : s; n = 0; b[n++] = s < 0 ? '-' : '+'; n += (unsigned)fmt_int(b + n, a / 48); b[n++] = '.'; b[n++] = (char)('0' + (a % 48) * 10 / 48); n = put(b, n, " OCT");
        tile(&t[0], "SPEED", b, (s + 192) * 100 / 240); tile_int(&t[1], "LEVEL", (int)le_get_level(), 0, 127); { int m = melodic_slot(); if (m >= 0) tile_int(&t[2], "OCTAVE", le_zone((unsigned)m)->oct, -3, 3); else tile(&t[2], "OCTAVE", "KIT", -1); } tile_int(&t[3], "ZONE", bank + 1, 1, (int)nbanks() > 1 ? (int)nbanks() : 2); break; }
    case PG_ENV: { char u[12]; fmt_ms(u, ui_ms(z->a)); tile(&t[0], "ATTACK", u, z->a * 100 / 127); fmt_ms(u, ui_ms(z->d)); tile(&t[1], "DECAY", u, z->d * 100 / 127);
        n = (unsigned)fmt_int(u, z->s * 100 / 127); u[n++] = '%'; u[n] = 0; tile(&t[2], "SUSTAIN", u, z->s * 100 / 127); fmt_ms(u, ui_ms(z->r)); tile(&t[3], "RELEASE", u, z->r * 100 / 127); break; }
    case PG_LFO: { unsigned p = (unsigned)z->lfo_rate * 32 / 127; static const uint16_t bb[4] = {1000, 1189, 1414, 1682}; unsigned tn = ((unsigned)bb[p & 3] << (p >> 2)) / 1000;
        n = (unsigned)fmt_int(b, (int)(tn / 10)); b[n++] = '.'; b[n++] = (char)('0' + tn % 10); n = put(b, n, "HZ"); tile(&t[0], "RATE", b, z->lfo_rate * 100 / 127); tile_int(&t[1], "DEPTH", z->lfo_depth, 0, 127);
        tile(&t[2], "SHAPE", shape[z->lfo_shape & 3], -1); tile(&t[3], "TARGET", target[z->lfo_target % 3], -1); break; }
    case PG_EDIT: { uint32_t F = sl ? sl->frames : 1, e = z->end ? z->end : F; char u[12];
        fmt_ms(u, (unsigned)((uint64_t)z->start * 1000 / LE_SRC_RATE)); tile(&t[0], "START", u, (int)((uint64_t)z->start * 100 / F)); fmt_ms(u, (unsigned)((uint64_t)e * 1000 / LE_SRC_RATE)); tile(&t[1], "END", u, (int)((uint64_t)e * 100 / F));
        tile_int(&t[2], "VOLUME", z->vol, 0, 127); tile(&t[3], "REVERSE", z->rev ? "ON" : "OFF", -1); break; }
    case PG_GLO: tile_int(&t[0], "BPM", st.bpm, 40, 240); n = (unsigned)fmt_int(b, st.beats); b[n++] = '/'; b[n++] = '4'; b[n] = 0; tile(&t[1], "MEASURE", b, -1);
        tile(&t[2], "ROOT", le_get_scale() ? note_nm[le_get_scale_root()] : "-", -1); tile(&t[3], "SCALE", le_scale_name(le_get_scale()), -1); break;
    case PG_FX: { uint8_t *p = le_fx_params(fx_sel);
        if (fx_sel == FX_REVERB) { tile_int(&t[0], "SIZE", p[0], 0, 127); tile_int(&t[1], "DAMP", p[1], 0, 127); tile_int(&t[2], "RETURN", p[2], 0, 127); }
        else if (fx_sel == FX_DELAY) { tile(&t[0], "TIME", dly[p[0] * 8 / 128], p[0] * 100 / 127); tile_int(&t[1], "FEEDBACK", p[1], 0, 127); tile_int(&t[2], "TONE", p[2], 0, 127); }
        else if (fx_sel == FX_CRUSH) { tile_int(&t[0], "BITS", 2 + p[0] * 14 / 127, 2, 16); tile_int(&t[1], "RATE", 1 + p[1] * 31 / 127, 1, 32); tile_int(&t[2], "RETURN", p[2], 0, 127); }
        else { tile_int(&t[0], "CUTOFF", p[0], 0, 127); tile_int(&t[1], "RESO", p[1], 0, 127); tile(&t[2], "TYPE", p[2] < 43 ? "LOWPASS" : p[2] < 86 ? "HIGHPASS" : "BANDPASS", p[2] * 100 / 127); }
        tile_int(&t[3], "SEND", z->send[fx_sel], 0, 127); break; }
    case PG_SEL: tile(&t[0], "ZONE", sl ? sl->name : "-", -1); tile(&t[1], "STATE", z->mute ? "MUTED" : "ON", -1); tile(&t[2], "SOLO", "TURN", -1); tile(&t[3], "ALL ON", "TURN", -1); break;
    case PG_SAVE: tile(&t[0], "SAVE", "TURN", -1); tile(&t[1], "CLR PAT", "TURN", -1); tile(&t[2], "CLR ALL", "TURN", -1); tile(&t[3], "COPY TO", st.pattern < 7 ? "NEXT" : "-", -1); break;
    case PG_ARP: { le_arp_t *a = le_arp(); tile(&t[0], "RATE", ratn[a->rate & 3], -1); tile(&t[1], "DIRECT", dirn[a->dir & 3], -1); n = (unsigned)fmt_int(b, a->range); n = put(b, n, " OCT"); tile(&t[2], "RANGE", b, (a->range - 1) * 33);
        n = (unsigned)fmt_int(b, a->gate); b[n++] = '%'; b[n] = 0; tile(&t[3], "GATE", b, a->gate); break; }
    case PG_SEQ: { tile(&t[0], "QUANT", qn[st.quant & 3], -1); tile_int(&t[1], "BARS", st.bars, 1, 8); tile(&t[2], "METRO", metro[(st.metro ? 1 : 0) | (st.countin ? 2 : 0)], -1); tile(&t[3], "1ST TAKE", st.freemode ? "FREE" : "CLICK", -1); break; }
    }
}
static void page_adjust(unsigned pg, unsigned k, int d, uint32_t now)
{
    struct le_seq_status st; le_seq_status_get(&st); const lfp_info_t *in = le_info(); le_zone_t *z = le_zone((unsigned)az); const lfp_slot_t *sl = in && (unsigned)az < in->n_slots ? &in->slot[az] : 0;
    int sgn = d > 0 ? 1 : -1; if (d > 2 || d < -2) d *= 2; unsaved = pg != PG_SAVE && pg != PG_SEQ ? 1 : unsaved;
    switch (pg) {
    case PG_HOME: if (k == 0) le_set_speed(le_get_speed() + d * 2); else if (k == 1) le_set_level((unsigned)bump((int)le_get_level(), d, 0, 127)); else if (k == 2) { int m = melodic_slot(); if (m >= 0) { le_zone((unsigned)m)->oct = (int8_t)bump(le_zone((unsigned)m)->oct, sgn, -3, 3); unsaved = 1; } } else set_bank((int)bank + sgn); if (k <= 1) unsaved = 1; break;
    case PG_ENV: { uint8_t *p = k == 0 ? &z->a : k == 1 ? &z->d : k == 2 ? &z->s : &z->r; *p = (uint8_t)bump(*p, d, 0, 127); break; }
    case PG_LFO: if (k == 0) z->lfo_rate = (uint8_t)bump(z->lfo_rate, d, 0, 127); else if (k == 1) z->lfo_depth = (uint8_t)bump(z->lfo_depth, d, 0, 127); else if (k == 2) z->lfo_shape = (uint8_t)bump(z->lfo_shape, sgn, 0, 3); else z->lfo_target = (uint8_t)bump(z->lfo_target, sgn, 0, 2); break;
    case PG_EDIT: if (!sl) break;
        if (k == 0 || k == 1) { uint32_t F = sl->frames, step = F / 128 / 20 * 20; if (step < 20) step = 20; int64_t e = z->end ? z->end : F;
            if (k == 0) { int64_t s2 = (int64_t)z->start + (int64_t)d * step; if (s2 < 0) s2 = 0; if (s2 > e - 4 * (int64_t)step) s2 = e - 4 * (int64_t)step; if (s2 < 0) s2 = 0; z->start = (uint32_t)s2; }
            else { e += (int64_t)d * step; if (e < (int64_t)z->start + 4 * (int64_t)step) e = (int64_t)z->start + 4 * step; z->end = e >= (int64_t)F ? 0 : (uint32_t)e; } }
        else if (k == 2) z->vol = (uint8_t)bump(z->vol, d, 0, 127); else z->rev = sgn > 0; break;
    case PG_GLO: if (k == 0) le_seq_set_bpm((unsigned)bump(st.bpm, d, 40, 240)); else if (k == 1) { if (st.state == LS_STOP) le_seq_set_beats((unsigned)bump(st.beats, sgn, 2, 7)); else say("STOP FIRST", now); }
        else if (k == 2) le_set_scale(le_get_scale(), (le_get_scale_root() + (sgn > 0 ? 1U : 11U)) % 12U); else le_set_scale((le_get_scale() + (sgn > 0 ? 1U : 7U)) % 8U, le_get_scale_root()); break;
    case PG_FX: if (k < 3) { uint8_t *p = &le_fx_params(fx_sel)[k]; *p = (uint8_t)bump(*p, d, 0, 127); } else z->send[fx_sel] = (uint8_t)bump(z->send[fx_sel], d, 0, 127); break;
    case PG_SEL: if (k == 0) step_az(sgn);
        else if (k == 1) { z->mute = sgn > 0; if (z->mute) le_zone_cut((unsigned)az); }
        else if (k == 2 && in) { for (unsigned i = 0; i < in->n_slots; i++) { le_zone(i)->mute = sgn > 0 && (int)i != az; if (le_zone(i)->mute) le_zone_cut(i); } if (sgn > 0) le_zone((unsigned)az)->mute = 0; }
        else if (k == 3 && in) for (unsigned i = 0; i < in->n_slots; i++) le_zone(i)->mute = 0; break;
    case PG_SAVE: if (sgn > 0) { if (k == 0) say(tail_save() == 0 ? "SAVED" : "NOT SAVED", now); else if (k == 1) { le_seq_clear(); say("PATTERN CLEARED", now); } else if (k == 2) { le_seq_clear_all(); say("ALL CLEARED", now); } else if (st.pattern < 7) { le_seq_copy((unsigned)st.pattern + 1); say("COPIED", now); } } break;
    case PG_ARP: { le_arp_t *a = le_arp(); if (k == 0) a->rate = (uint8_t)bump(a->rate, sgn, 0, 3); else if (k == 1) a->dir = (uint8_t)bump(a->dir, sgn, 0, 3); else if (k == 2) a->range = (uint8_t)bump(a->range, sgn, 1, 4); else a->gate = (uint8_t)bump(a->gate, d * 5, 5, 100); break; }
    case PG_SEQ: if (k == 0) le_seq_set_quant((unsigned)bump(st.quant, sgn, 0, 3)); else if (k == 1) { static const uint8_t o[4] = {1, 2, 4, 8}; int i = 0; while (i < 3 && o[i] != st.bars) ++i; le_seq_set_bars(o[bump(i, sgn, 0, 3)]); }
        else if (k == 2) { int m = (st.metro ? 1 : 0) | (st.countin ? 2 : 0); m = bump(m, sgn, 0, 3); le_seq_set_metro(m & 1, m >> 1 & 1); } else le_seq_set_free((unsigned)bump(st.freemode, sgn, 0, 1)); break;
    }
    dirty |= (pg == PG_HOME && k < 3) || pg == PG_FX ? 1 : 3;     /* only the tiles + header when the picture does not depend on the knob (no flicker) */
}
static void page_select(unsigned pg, int d, uint32_t now)       /* the SELECT knob */
{
    int sgn = d > 0 ? 1 : -1; struct le_seq_status st; le_seq_status_get(&st);
    switch (pg) {
    case PG_FX: fx_sel = (uint8_t)bump(fx_sel, sgn, 0, FX_COUNT - 1); break;
    case PG_ARP: { le_arp_t *a = le_arp(); uint8_t on = sgn > 0; if (a->on != on) { a->on = on; if (!on) le_arp_release(); } unsaved = 1; break; }
    case PG_SEQ: case PG_SAVE: le_seq_select((unsigned)bump(st.pattern, sgn, 0, LS_PATTERNS - 1)); break;
    case PG_GLO: break;
    default: step_az(sgn); break;
    }
    (void)now; dirty |= 3;
}
static uint8_t upload_drawn; static unsigned upload_pct_last = 999;
static void draw_upload(void)      /* the static part once, afterwards only the percentage and the bar */
{
    char b[16]; unsigned total = dev.total ? dev.total : 1, pct = dev.state == LF_UPLOADING ? dev.next * 100U / total : (dev.state == LF_PACK ? 100U : 0U);
    if (!upload_drawn) { gfx_reset(); gfx_fill(0, 0, 240, 240, COL_BG); draw_text(8, 60, 3, "UPLOAD", COL_ACC, COL_BG); draw_text(8, 190, 1, "KEEP THE PHONE CLOSE", COL_DIM, COL_BG); upload_drawn = 1; upload_pct_last = 999; }
    if (pct == upload_pct_last) return; upload_pct_last = pct;
    unsigned n = (unsigned)fmt_int(b, (int)pct); b[n++] = '%'; b[n] = 0; draw_pad(8, 110, 4, b, 4, COL_FG, COL_BG);
    gfx_fill(8, 160, 224, 14, COL_BAR); if (pct) gfx_fill(8, 160, 224 * pct / 100, 14, COL_ACC);
}

/* ---- protocol pump -------------------------------------------------------------------------------------------- */
static void protocol_pump(uint32_t now)
{
    if (!flash_ready) {
        uint32_t size = fm1_flash_size();
        if (size >= 6 * LF_SECTOR) {
            dev.fl = (lf_flash_t){0, size, fl_read, fl_write, fl_erase}; lf_dev_init(&dev); flash_ready = 1; bound_seen = (uint8_t)(bind_pack() == 0); dirty |= 3;
        }
        return;
    }
    if (reply_len) { if (fm1_midi_send_port(reply_port, reply_buf, reply_len) == -2) return; reply_len = 0; }   /* a busy port: retry next tick */
    unsigned handled = 0;
    while (rxq[rx_tail].length && handled < 8) {
        const uint8_t *m = rxq[rx_tail].data; unsigned n = rxq[rx_tail].length;
        unsigned cmd = n > 5 ? m[5] : 0;
        if (cmd == LF_BEGIN || cmd == LF_DELETE || cmd == LF_ABORT) { le_all_off(); (void)le_bind(NULL, 0); bound_seen = 0; dirty |= 3; }
        size_t r = lf_dev_handle(&dev, m, n, reply_buf);
        upload_seen = now; upload_active = 1;
        if (r) { reply_len = (uint8_t)r; reply_port = rxq[rx_tail].source & 3U; if (fm1_midi_send_port(reply_port, reply_buf, r) != -2) reply_len = 0; }
        if (cmd == LF_END || cmd == LF_DELETE || cmd == LF_ABORT) { bound_seen = (uint8_t)(bind_pack() == 0); az = 0; dirty |= 3; }
        rxq[rx_tail].length = 0; rx_tail = (uint8_t)((rx_tail + 1) % QN); ++handled;
        if (reply_len) break;
    }
    if (upload_active && now - upload_seen > 1500U) { upload_active = 0; upload_drawn = 0; gfx_reset(); dirty |= 3; if (!bound_seen) bound_seen = (uint8_t)(bind_pack() == 0); }
}

/* ---- drawing of the pages -------------------------------------------------------------------------------------------- */
static const char *const state_name[5] = {"STOP", "COUNT", "PLAY", "REC", "FREE"};
static void draw_header(int force)
{
    struct le_seq_status st; le_seq_status_get(&st); const lfp_info_t *in = le_info(); char b[24]; unsigned n;
    uint32_t key = (uint32_t)st.state | (uint32_t)st.pattern << 3 | (uint32_t)st.bpm << 8 | (uint32_t)page << 20 | (uint32_t)unsaved << 25 | (uint32_t)bank << 26;
    if (!force && hdr_key_valid && key == hdr_key) return;
    hdr_key = key; hdr_key_valid = 1;
    gfx_fill(0, 0, 240, 26, COL_BAR); draw_text(6, 5, 2, page_name[page], COL_ACC, COL_BAR);
    if (in) { draw_pad(82, 9, 1, in->name, 12, COL_FG, COL_BAR); }
    n = 0; b[n++] = (char)('A' + st.pattern); b[n++] = ' '; n += (unsigned)fmt_int(b + n, st.bpm); b[n] = 0; draw_text(160, 5, 1, b, COL_DIM, COL_BAR);
    draw_text(160, 15, 1, state_name[st.state > 4 ? 0 : st.state], st.state == LS_REC || st.state == LS_FREE ? 0xf800 : (st.state ? COL_OK : COL_DIM), COL_BAR);
    n = 0; b[n++] = 'Z'; n += (unsigned)fmt_int(b + n, bank + 1); b[n] = 0; draw_text(208, 15, 1, b, COL_FG, COL_BAR);
    if (unsaved) draw_text(226, 5, 1, "*", COL_ACC, COL_BAR);
}
static void draw_wave(unsigned y0, unsigned h, int markers)       /* the active zone; start / end markers on EDIT */
{
    const lfp_info_t *in = le_info(); if (!in || (unsigned)az >= in->n_slots) return; const lfp_slot_t *s = &in->slot[az]; le_zone_t *z = le_zone((unsigned)az);
    uint16_t col = zone_col[az % 12]; unsigned cy = y0 + h / 2;
    gfx_fill(10, cy, 220, 1, COL_BAR);
    if (!wave_ready) { gfx_fill(10, y0, 220, h, COL_BG); gfx_fill(10, cy, 220, 1, COL_BAR); unsigned pct = wave_sb * LOFI_SUPER * 100 / (s->frames ? s->frames : 1); char b[8]; fmt_int(b, (int)pct); unsigned n = 0; while (b[n]) ++n; b[n++] = '%'; b[n] = 0; draw_text(10, y0 + 2, 1, b, COL_DIM, COL_BG); }
    for (unsigned c = 0; c < 220; ++c) {
        int hi = wmax[c] * (int)h / 256, lo = -wmin[c] * (int)h / 256; if (hi < 0) hi = 0; if (lo < 0) lo = 0;
        unsigned x = 10 + c; uint16_t cc = col;
        if (markers) { unsigned sx = (unsigned)((uint64_t)z->start * 220 / s->frames), ex = z->end ? (unsigned)((uint64_t)z->end * 220 / s->frames) : 220; if (c < sx || c >= ex) cc = COL_DIM; }
        gfx_fill(x, cy - (unsigned)hi, 1, (unsigned)(hi + lo + 1), cc);
    }
    if (markers) { unsigned sx = (unsigned)((uint64_t)z->start * 220 / s->frames), ex = z->end ? (unsigned)((uint64_t)z->end * 220 / s->frames) : 219; gfx_fill(10 + sx, y0, 1, h, 0xffff); gfx_fill(10 + (ex > 219 ? 219 : ex), y0, 1, h, 0xffff); }
    if (z->rev) draw_text(10, y0 + h - 8, 1, "<<<<", COL_ACC, COL_BG);
}
static void draw_zone_line(unsigned y)
{
    const lfp_info_t *in = le_info(); char b[40]; unsigned n = 0; if (!in || (unsigned)az >= in->n_slots) return; const lfp_slot_t *s = &in->slot[az];
    gfx_fill(0, y, 240, 12, COL_BG); gfx_fill(6, y + 1, 5, 9, zone_col[az % 12]);
    n += (unsigned)fmt_int(b + n, az + 1); b[n++] = ' '; n = put(b, n, s->name); b[n++] = ' '; b[n++] = ' ';
    n = put(b, n, s->hi != s->lo ? "KEYS " : "KEY "); n += (unsigned)fmt_int(b + n, s->lo % 32 + 1); if (s->hi != s->lo) { b[n++] = '-'; n += (unsigned)fmt_int(b + n, s->hi % 32 + 1); } b[n] = 0;
    draw_text(16, y + 2, 1, b, COL_FG, COL_BG);
}
/* length in ticks of the note that starts with event i. The list is in recording order, so the release of a note is the next event of the same pitch AFTER it in the list
 * (if that is another press, the release was never recorded). Matching by time instead (the nearest event of the same pitch) breaks as soon as passes overlap (overdub of the same
 * key: press, press, release, release) and draws notes too short or as one long bar. Returns 0 when there is no recorded release. */
static unsigned seq_pair_len(const struct le_seq_event *ev, unsigned cnt, unsigned i, unsigned L)
{
    for (unsigned j = i + 1; j < cnt; ++j) if (ev[j].note == ev[i].note) { if (ev[j].on) return 0; unsigned d = (ev[j].tick % L + L - ev[i].tick % L) % L; return d ? d : L; }
    return 0;
}
static unsigned seq_note_len(const struct le_seq_event *ev, unsigned cnt, unsigned i, unsigned L)
{
    unsigned p = seq_pair_len(ev, cnt, i, L); if (p) return p;
    unsigned best = 0;     /* no release in the list after it: up to the nearest later event of the same pitch, round the loop */
    for (unsigned j = 0; j < cnt; ++j) if (j != i && ev[j].note == ev[i].note) { unsigned d = (ev[j].tick + L - ev[i].tick) % L; if (d > 0 && (!best || d < best)) best = d; }
    return best ? best : 12;
}
/* the piano roll of the selected pattern. c0..c1 = columns to (re)draw; playhead drawn when on */
static int seq_ph = -1; static unsigned seq_evn;
static void seq_cfill(int x, int y, int w, int h, uint16_t c, int c0, int c1) { int x1 = x + w; if (x < c0) x = c0; if (x1 > c1) x1 = c1; if (x1 > x) gfx_fill((unsigned)x, (unsigned)y, (unsigned)(x1 - x), (unsigned)h, c); }
static int free_secs = -1;
static int free_key(const struct le_seq_status *st) { return st->armed ? 0 : 1 + (int)((uint64_t)st->tick * 60 / ((uint64_t)st->bpm * LS_TICKS_BEAT)); }       /* 0 = waiting, else 1 + whole seconds: the clock redraws once a second */
static void seq_free_draw(const struct le_seq_status *st)     /* the free first take has no loop yet: a big clock instead of the roll */
{
    char b[16]; unsigned n = 0; int sec = free_key(st) - 1; free_secs = free_key(st);
    gfx_fill(0, 30, 240, 118, COL_BG);
    draw_text(20, 48, 2, st->armed ? "PLAY THE FIRST NOTE" : "FREE TAKE", COL_FG, COL_BG);
    if (!st->armed) { n = (unsigned)fmt_int(b, sec); b[n++] = ' '; b[n++] = 'S'; b[n] = 0; draw_text(20, 78, 3, b, 0xf800, COL_BG); }
    draw_text(8, 124, 1, "REC ENDS THE TAKE  LENGTH SETS BPM", COL_DIM, COL_BG);
}
static void seq_draw(int c0, int c1, int playhead)
{
    struct le_seq_status st; le_seq_status_get(&st); const lfp_info_t *in = le_info(); struct le_seq_event ev[LS_MAX_EVENTS];
    if (st.state == LS_FREE) { seq_free_draw(&st); return; }
    unsigned cnt = le_seq_events(st.pattern, ev, LS_MAX_EVENTS); seq_evn = st.events;
    /* one row per pitch that is really used (sorted): notes of different zones are 32 ids apart, a raw pitch scale squeezed them into the same rows and
     * neighbouring notes ran together into one long bar. Over 25 different pitches several share a row (by rank). */
    static uint8_t used[256]; memset(used, 0, sizeof used); unsigned nd = 0;
    for (unsigned i = 0; i < cnt; ++i) if (!used[ev[i].note]) { used[ev[i].note] = 1; ++nd; }
    uint8_t rank[256]; unsigned r = 0; for (unsigned n = 0; n < 256; ++n) { rank[n] = r; if (used[n]) ++r; }
    unsigned rows = nd < 12 ? 12 : nd > 25 ? 25 : nd, rh = 112 / rows; if (rh < 3) rh = 3; if (rh > 14) rh = 14;
    unsigned L = st.loop_ticks ? st.loop_ticks : 384; uint32_t tpb = LS_TICKS_BEAT; int H = (int)(rh * rows + 2);
    for (unsigned bt = 0; bt * tpb < L; ++bt) seq_cfill(10 + (int)((uint64_t)bt * tpb * 220 / L), 30, 1, H, (bt % st.beats) == 0 ? COL_DIM : COL_BAR, c0, c1);
    for (unsigned i = 0; i < cnt; ++i) if (ev[i].on) {
        unsigned t0 = ev[i].tick % L, len = seq_pair_len(ev, cnt, i, L), wrap = 0;
        if (!len) len = st.state == LS_REC ? ((st.tick + L - t0) % L < 6 ? 6 : (st.tick + L - t0) % L) : seq_note_len(ev, cnt, i, L);     /* a key still held while recording: the bar grows with the playhead */
        unsigned t1 = t0 + len; if (t1 > L) { wrap = t1 - L; t1 = L; }
        int x0 = 10 + (int)((uint64_t)t0 * 220 / L), x1 = 10 + (int)((uint64_t)t1 * 220 / L); if (x1 > 230) x1 = 230;
        if (x1 - x0 > 3) --x1;                          /* a gap before the next note of the same pitch: repeated notes stay separate bars */
        if (x1 <= x0) x1 = x0 + 2;
        unsigned row = nd > 25 ? (nd - 1 - rank[ev[i].note]) * 24 / (nd - 1) : nd - 1 - rank[ev[i].note]; int z = zone_of_note(in, ev[i].note); uint16_t col = z >= 0 ? zone_col[z % 12] : COL_FG; int ry = 31 + (int)row * (int)rh, rhh = rh > 2 ? (int)rh - 1 : (int)rh;
        seq_cfill(x0, ry, x1 - x0, rhh, col, c0, c1);
        seq_cfill(x0, ry, 1, rhh, 0xffff, c0, c1);      /* the start of a note is a white tick */
        if (wrap) { int xw = 10 + (int)((uint64_t)wrap * 220 / L); if (xw < 12) xw = 12; seq_cfill(10, ry, xw - 10, rhh, col, c0, c1); } }       /* a note held over the loop point continues at the start */
    if (playhead && st.state) { seq_ph = 10 + (int)((uint64_t)st.tick * 220 / L); seq_cfill(seq_ph, 30, 2, H, 0xffff, c0, c1); } else seq_ph = -1;
    if (!cnt && c0 <= 40 && c1 >= 200) draw_text(40, 80, 1, "EMPTY  PRESS REC AND PLAY", COL_DIM, COL_BG);
}
static void seq_playhead(void)               /* move the playhead without redrawing the roll */
{
    struct le_seq_status st; le_seq_status_get(&st); if (!st.state) return;
    if (st.state == LS_FREE) { if (free_key(&st) != free_secs) seq_free_draw(&st); return; }
    if (st.events != seq_evn) { seq_ph = -1; gfx_fill(0, 30, 240, 118, COL_BG); seq_draw(0, 240, 1); return; }      /* a note was recorded / removed: the whole roll, partial strips would keep stale bars */
    if (free_secs >= 0) { free_secs = -1; dirty |= 2; return; }      /* the free take just ended: the roll again */
    unsigned L = st.loop_ticks ? st.loop_ticks : 384; int nx = 10 + (int)((uint64_t)st.tick * 220 / L);
    if (nx == seq_ph) return;
    if (seq_ph >= 0) { gfx_fill((unsigned)seq_ph - 1, 30, 5, 118, COL_BG); seq_draw(seq_ph - 1, seq_ph + 4, 0); }
    gfx_fill((unsigned)nx - 1, 30, 5, 118, COL_BG); seq_draw(nx - 1, nx + 4, 1);
}
static void draw_vis(void)
{
    const lfp_info_t *in = le_info(); char b[32]; unsigned n; le_zone_t *z = le_zone((unsigned)az);
    if (!in && page != PG_GLO && page != PG_SEQ) {
        draw_text(8, 60, 2, "NO PACK", COL_FG, COL_BG); draw_text(8, 90, 1, "OPEN THE LOFI APP ON YOUR PHONE", COL_DIM, COL_BG); draw_text(8, 104, 1, "AND SEND A SAMPLE PACK OVER", COL_DIM, COL_BG); draw_text(8, 118, 1, "BLUETOOTH", COL_DIM, COL_BG); return;
    }
    switch (page) {
    case PG_HOME: draw_strip(in, 32); draw_zone_line(58); draw_wave(74, 70, 0); break;
    case PG_EDIT: draw_zone_line(30); draw_wave(46, 98, 1); break;
    case PG_ENV: {
        draw_zone_line(30); unsigned wa = 12 + z->a * 38 / 127, wd = 12 + z->d * 38 / 127, ws = 36, wr = 12 + z->r * 38 / 127, top = 56, bot = 140, sy = bot - (unsigned)z->s * (bot - top) / 127; unsigned x = 14;
        gfx_fill(10, bot + 1, 220, 1, COL_BAR); line((int)x, (int)bot, (int)(x + wa), (int)top, zone_col[az % 12]); x += wa; line((int)x, (int)top, (int)(x + wd), (int)sy, zone_col[az % 12]); x += wd;
        line((int)x, (int)sy, (int)(x + ws), (int)sy, zone_col[az % 12]); x += ws; line((int)x, (int)sy, (int)(x + wr), (int)bot, zone_col[az % 12]);
        break; }
    case PG_LFO: {
        draw_zone_line(30); gfx_fill(10, 98, 220, 1, COL_BAR); int amp = 8 + z->lfo_depth * 36 / 127, px = 0, py = 0;
        for (int xx = 0; xx < 216; xx += 3) { uint32_t ph = (uint32_t)xx * (0xffffffffu / 108u); int32_t t = (int32_t)(ph >> 16) & 0xffff, v;
            if (z->lfo_shape == 1) { int32_t u = t - 32768; if (u < 0) u = -u; v = (u - 16384) * 2; } else if (z->lfo_shape == 2) v = t < 32768 ? 32767 : -32767; else if (z->lfo_shape == 3) v = t - 32768; else { int32_t u = t & 0x7fff, y = (u * (32768 - u)) >> 13; v = t < 32768 ? y : -y; }
            int yy = 98 - v * amp / 32768; if (xx) line(12 + px, py, 12 + xx, yy, zone_col[az % 12]); px = xx; py = yy; }
        break; }
    case PG_FX: {
        for (unsigned i = 0; i < FX_COUNT; ++i) { int on = i == fx_sel; gfx_fill(6 + i * 58, 30, 54, 18, on ? COL_ACC : COL_BAR); draw_text(10 + i * 58, 35, 1, fx_name[i], on ? COL_BG : COL_FG, on ? COL_ACC : COL_BAR); }
        draw_text(8, 56, 1, "SEND OF ZONE", COL_DIM, COL_BG); if (in && (unsigned)az < in->n_slots) { draw_text(104, 56, 1, in->slot[az].name, zone_col[az % 12], COL_BG); }
        draw_text(8, 76, 1, "KEYS ARE PUNCH IN EFFECTS", COL_DIM, COL_BG);
        for (unsigned i = 0; i < PN_COUNT; ++i) { unsigned x = 6 + (i % 6) * 38, y = 92 + (i / 6) * 26; int on = down_keys >> i & 1U; gfx_fill(x, y, 34, 22, on ? COL_ACC : COL_BAR); draw_text(x + 4, y + 7, 1, punch_name[i], on ? COL_BG : COL_FG, on ? COL_ACC : COL_BAR); }
        break; }
    case PG_SEL: {
        draw_strip(in, 32); unsigned idx[32], cnt = 0, pos = 0; for (unsigned i = 0; i < in->n_slots && cnt < 32; ++i) if (in_bank(i)) { if ((int)i == az) pos = cnt; idx[cnt++] = i; }
        unsigned first = pos > 2 ? pos - 2 : 0;
        for (unsigned row = 0; row < 5 && first + row < cnt; ++row) { unsigned i = idx[first + row], y = 62 + row * 17; int on = (int)i == az; uint16_t bg = on ? COL_BAR : COL_BG; gfx_fill(0, y, 240, 15, bg);
            gfx_fill(4, y + 2, 5, 11, le_zone(i)->mute ? COL_DIM : zone_col[i % 12]); draw_pad(16, y + 4, 1, in->slot[i].name, 12, le_zone(i)->mute ? COL_DIM : COL_FG, bg); draw_text(150, y + 4, 1, le_zone(i)->mute ? "MUTED" : "ON", le_zone(i)->mute ? COL_DIM : COL_OK, bg); }
        break; }
    case PG_GLO: {
        struct le_seq_status st; le_seq_status_get(&st); draw_text(8, 36, 1, "KEYS OF THE SCALE", COL_DIM, COL_BG);
        for (unsigned k = 0; k < 12; ++k) { unsigned pc = (k + le_get_scale_root()) % 12, inscale = 0; unsigned sc = le_get_scale();
            (void)pc; static const uint8_t st_[8][12] = {{1,1,1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1,1,1}, {1,0,1,0,1,1,0,1,0,1,1,0}, {1,0,1,0,1,1,0,1,0,1,0,1}, {1,0,1,1,0,1,0,1,1,0,1,0}, {1,0,1,1,0,1,0,1,1,0,0,1}, {1,1,0,1,0,1,0,1,1,0,1,0}, {1,0,1,1,0,1,0,1,1,0,1,1}};
            inscale = st_[sc][k]; uint16_t c = k == 0 && sc ? COL_ACC : (inscale && sc ? COL_FG : COL_BAR); gfx_fill(6 + k * 19, 50, 17, 30, c); draw_text(9 + k * 19, 85, 1, note_nm[(k + (sc ? le_get_scale_root() : 0)) % 12], COL_DIM, COL_BG); }
        draw_text(8, 104, 1, "BEATS IN A BAR", COL_DIM, COL_BG); for (unsigned i = 0; i < st.beats; ++i) gfx_fill(8 + i * 24, 118, 18, 18, i == 0 ? COL_ACC : COL_FG);
        break; }
    case PG_SAVE: {
        struct le_seq_status st; le_seq_status_get(&st); draw_text(8, 32, 1, "KEYS PICK A PATTERN", COL_DIM, COL_BG);
        for (unsigned i = 0; i < LS_PATTERNS; ++i) { unsigned x = 6 + (i % 4) * 58, y = 46 + (i / 4) * 46; int on = i == st.pattern; unsigned cnt = le_seq_pattern_count(i); gfx_fill(x, y, 54, 40, on ? COL_ACC : COL_BAR);
            b[0] = (char)('A' + i); b[1] = 0; draw_text(x + 4, y + 4, 2, b, on ? COL_BG : COL_FG, on ? COL_ACC : COL_BAR); n = (unsigned)fmt_int(b, (int)cnt); b[n++] = 'E'; b[n++] = 'V'; b[n] = 0; draw_text(x + 4, y + 28, 1, b, on ? COL_BG : (cnt ? COL_OK : COL_DIM), on ? COL_ACC : COL_BAR); }
        if (msg_until) draw_text(8, 140, 1, msg_text, COL_ACC, COL_BG);
        break; }
    case PG_ARP: { le_arp_t *a = le_arp(); draw_text(8, 40, 3, a->on ? "ARP ON" : "ARP OFF", a->on ? COL_OK : COL_DIM, COL_BG); draw_text(8, 76, 1, "SELECT TURNS IT ON AND OFF", COL_DIM, COL_BG); draw_text(8, 92, 1, "HOLD KEYS FOR THE CHORD", COL_DIM, COL_BG);
        for (unsigned i = 0; i < 8; ++i) { unsigned h = 8 + (a->dir == 1 ? 7 - i : a->dir == 2 ? (i < 4 ? i : 7 - i) : i) * 6; gfx_fill(10 + i * 26, 140 - h, 20, h, a->on ? COL_ACC : COL_BAR); } break; }
    case PG_SEQ: seq_draw(0, 240, 1); break;
    }
}
static void draw_tiles(void)
{
    tile_t t[4]; page_tiles(page, t);
    for (unsigned i = 0; i < 4; ++i) {
        unsigned y = 150 + i * 22; gfx_fill(0, y, 240, 22, COL_BG);
        if (!t[i].name[0]) continue;
        draw_text(6, y + 8, 1, t[i].name, COL_DIM, COL_BG); draw_text(66, y + 3, 2, t[i].val, i == 3 && page == PG_FX ? COL_OK : COL_ACC, COL_BG);
        if (t[i].frac >= 0) { gfx_fill(190, y + 8, 44, 6, COL_BAR); unsigned w = (unsigned)(t[i].frac > 100 ? 100 : t[i].frac) * 44 / 100; if (w) gfx_fill(190, y + 8, w, 6, COL_FG); }
    }
}
static void draw_page(unsigned d)       /* tiles first (they change while a knob is turned), then the header and the picture; all of it is only queued */
{
    draw_tiles(); draw_header(1); if (d & 2) { gfx_fill(0, 26, 240, 124, COL_BG); draw_vis(); }
}
/* ---- LEDs: the active page button alone, keys by what they do on the page ---- */
static void leds_refresh(uint32_t now)
{
    struct le_seq_status st; le_seq_status_get(&st); const lfp_info_t *in = le_info(); uint64_t want = 1ull << page_btn[page];
    if (st.state != LS_STOP) want |= 1ull << BTN_PLAY;
    if (st.state == LS_REC || (st.state == LS_FREE && (!st.armed || (now / 250U & 1U))) || (st.state == LS_COUNTIN && (now / 250U & 1U))) want |= 1ull << BTN_REC;
    for (unsigned k = 0; k < KEYS; ++k) {
        int zz = zone_of_note(in, (int)bank * 32 + (int)k); int on;
        switch (page) { case PG_FX: on = k < PN_COUNT; break; case PG_SAVE: on = k < LS_PATTERNS && (le_seq_pattern_count(k) || k == st.pattern); break; case PG_SEL: on = zz >= 0 && !le_zone((unsigned)zz)->mute; break; default: on = zz >= 0; }
        if (page != PG_FX && page != PG_SAVE && page != PG_SEL && (sounding >> k & 1U)) on = !on;     /* a key that is sounding flips: the lit keys of the zone blink off while they play */
        if (on) want |= 1ull << (14 + k);
    }
    uint64_t diff = led_force ? ((1ull << 41) - 1) : (want ^ led_have); led_force = 0;
    for (unsigned i = 0; diff && i < 41; ++i) if (diff >> i & 1ull) { fm1_led_set(i, (int)(want >> i & 1ull)); diff &= ~(1ull << i); }
    led_have = want;
}
static void key_press(unsigned k, uint32_t now)
{
    const lfp_info_t *in = le_info(); int note = (int)bank * 32 + (int)k, zz = zone_of_note(in, note); (void)now;
    key_act[k] = 0; down_keys |= 1UL << k;
    switch (page) {
    case PG_SEL: if (zz >= 0) { le_zone_t *z = le_zone((unsigned)zz); z->mute = !z->mute; if (z->mute) le_zone_cut((unsigned)zz); az = zz; unsaved = 1; } key_act[k] = 4; dirty |= 3; break;
    case PG_FX: if (k < PN_COUNT) { le_punch(k, 1); key_act[k] = 3; dirty |= 3; } break;
    case PG_SAVE: if (k < LS_PATTERNS) { le_seq_select(k); key_act[k] = 4; dirty |= 3; } break;
    default:
        held_note[k] = (int8_t)note; if (zz >= 0 && zz != az) { az = zz; dirty |= 3; }
        if (le_arp()->on) { le_arp_note_on((unsigned)note, 100); key_act[k] = 2; } else { le_note_on((unsigned)note, 100); key_act[k] = 1; }
        break;
    }
}
static void key_release(unsigned k)
{
    switch (key_act[k]) { case 1: le_note_off((unsigned)held_note[k]); break; case 2: le_arp_note_off((unsigned)held_note[k]); break; case 3: le_punch(k, 0); dirty |= 3; break; default: break; }
    key_act[k] = 0; down_keys &= ~(1UL << k);
}

void groove_screen_init(void)
{
    down_keys = 0; led_have = 0; led_force = 1; bank = 0; az = 0; page = PG_HOME; dirty = 3; flash_ready = 0; bound_seen = 0; upload_active = 0; reply_len = 0; unsaved = 0; fx_sel = 0; wave_slot = -1; hdr_key_valid = 0; msg_until = 0;
    rx_head = rx_tail = 0; mq_w = mq_r = 0; rx_frames = rx_dropped = 0;
    for (unsigned i = 0; i < QN; ++i) rxq[i].length = 0;
    sounding = 0; memset(held_note, 0, sizeof held_note); memset(key_act, 0, sizeof key_act);
    le_init();
    fm1_controls_init();
    for (unsigned key = 0; key < 41; ++key) fm1_led_set(key, 0);
    fm1_screen_init(); gfx_reset();
    gfx_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, COL_BG);
    draw_text(60, 100, 3, "LOFI", COL_ACC, COL_BG);
}

void groove_screen_control(void)
{
    struct fm1_controls c; uint32_t now = fm1_millis(); static uint32_t prevb;
    fm1_controls_read(&c);
    (void)groove_master_update(c.knob);           /* the physical MASTER: without it the output gain stays 0 */
    groove_project_poll();                        /* only brings up BLE MIDI in this firmware */
    protocol_pump(now);

    /* MIDI in: notes play the zones (or the arpeggiator), pitch bend +-2 semitones, CC 20 = live speed (0..127 -> -4..+1 octave). */
    while (mq_r != mq_w) {
        const uint8_t *m = midiq[mq_r].m; unsigned st = m[0] & 0xf0U;
        if (st == 0x90 && m[2]) { if (le_arp()->on) le_arp_note_on(m[1], m[2]); else le_note_on(m[1], m[2]); }
        else if (st == 0x80 || st == 0x90) { if (le_arp()->on) le_arp_note_off(m[1]); le_note_off(m[1]); }
        else if (st == 0xe0) { int v = (int)(m[1] | m[2] << 7) - 8192; le_set_bend(v * 200 / 8192); }
        else if (st == 0xb0 && m[1] == 20) { le_set_speed(-192 + (int)m[2] * 240 / 127); dirty |= 3; }
        mq_r = (uint8_t)((mq_r + 1) % MIDIQ);
    }
    if (upload_active) {                         /* the transfer screen replaces the instrument while data is coming in */
        if (now - upload_draw > 300U) { upload_draw = now; draw_upload(); }
        gfx_flush(150); prevb = c.buttons; return;
    }
    wave_step();
    uint32_t pressed = c.buttons & ~prevb; prevb = c.buttons;
    /* the page buttons: each one is a screen of four parameters; the pressed one lights, the others go dark; again = back to HOME */
    for (unsigned p = 0; p < PG_COUNT; ++p) if (pressed >> page_btn[p] & 1U) { page = (page == p && p != PG_HOME) ? PG_HOME : (uint8_t)p; dirty |= 3; hdr_key_valid = 0; gfx_reset(); }
    if (pressed >> BTN_OCT_DOWN & 1U) { set_bank((int)bank - 1); dirty |= 3; wave_slot = -1; }
    if (pressed >> BTN_OCT_UP & 1U) { set_bank((int)bank + 1); dirty |= 3; wave_slot = -1; }
    struct le_seq_status st; le_seq_status_get(&st);
    if (pressed >> BTN_PLAY & 1U) { if (st.state == LS_REC || st.state == LS_COUNTIN || st.state == LS_FREE) le_seq_rec(0); else if (st.state == LS_STOP) le_seq_play(); else le_seq_stop(); }     /* recording on: stop recording, keep playing */
    if (pressed >> BTN_REC & 1U) { if (st.state == LS_REC || st.state == LS_COUNTIN || st.state == LS_FREE) le_seq_rec(0); else le_seq_rec(1); }
    /* encoders: SELECT, ALGORITHM (zone of the sends on FX), K1..K4 = the four tiles */
    if (c.encoders[0]) page_select(page, c.encoders[0], now);
    if (c.encoders[1] && page == PG_FX && nz()) { step_az(c.encoders[1] > 0 ? 1 : -1); dirty |= 3; }
    for (unsigned k = 0; k < 4; ++k) if (c.encoders[2 + k]) page_adjust(page, k, c.encoders[2 + k], now);
    /* keys */
    uint32_t keys = c.keys & ((1UL << KEYS) - 1U), kd = keys ^ down_keys;
    for (unsigned i = 0; i < KEYS && kd; ++i) if (kd >> i & 1U) { if (keys >> i & 1U) key_press(i, now); else key_release(i); }
    {   /* keys that sound right now (held, sequencer, arpeggiator, MIDI): their LED flips, the strip on the screen shows them tall */
        uint32_t snd = le_sounding_keys(bank) & ((1UL << KEYS) - 1U);
        if (snd != sounding) { uint32_t d = snd ^ sounding; sounding = snd; leds_refresh(now); if ((page == PG_HOME || page == PG_SEL) && le_info()) for (unsigned k = 0; k < KEYS; ++k) if (d >> k & 1U) draw_strip_key(le_info(), 32, k); }
    }
    if (msg_until && now > msg_until) { msg_until = 0; msg_text = ""; dirty |= 3; }
    if (dirty && gfx_idle()) { unsigned d = dirty; dirty = 0; last_draw = now; draw_page(d); leds_refresh(now); }
    else if (now - last_draw > 100U) {
        static uint32_t strip_keys = 0xffffffffu; last_draw = now; draw_header(0); leds_refresh(now);
        if ((page == PG_HOME || page == PG_SEL) && le_info() && strip_keys != (down_keys | sounding)) { uint32_t d = strip_keys ^ (down_keys | sounding); strip_keys = down_keys | sounding; for (unsigned k = 0; k < KEYS; ++k) if (strip_keys == 0xffffffffu || (d >> k & 1U)) draw_strip_key(le_info(), 32, k); }
        if (page == PG_SEQ && gfx_idle()) seq_playhead();
    }
    gfx_flush(150);                              /* a bounded amount of drawing per tick: the keys are read again in time */
}

void groove_screen_tap(uint32_t now) { (void)now; }
void groove_screen_mmc(unsigned command) { (void)command; }
