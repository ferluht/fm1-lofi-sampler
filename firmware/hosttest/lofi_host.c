/* Host run of the real app/lofi_ui.c + lofi_engine.c: SDK stubs, a flash array, scripted panel input, screenshots and checks.
 * The pack is uploaded through lofi_sysex() with the C reference sender, so the SysEx queue, protocol and flash glue are the firmware's own. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1.h"
#include "fm1_device.h"
#include "lofi_ui.c"
#include "lofi_sender.h"

#define FLASH_BYTES 536576u
static unsigned long calls_tick, px_tick, calls_max, px_max, calls_total;
static uint8_t flash[FLASH_BYTES]; static struct fm1_controls ctl; static uint32_t ms; static uint16_t fb[240 * 240]; static int led[64];
void fm1_controls_init(void) {}
void fm1_controls_read(struct fm1_controls *s) { *s = ctl; memset(ctl.encoders, 0, sizeof ctl.encoders); }
void fm1_led_set(unsigned k, int on) { if (k < 64) led[k] = on; }
uint32_t fm1_millis(void) { return ms; }
void fm1_control_request(void) {}
void fm1_screen_init(void) {}
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t c) { calls_tick++; px_tick += (unsigned long)w * h; for (unsigned j = y; j < y + h && j < 240; j++) for (unsigned i = x; i < x + w && i < 240; i++) fb[j * 240 + i] = c; }
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *p, size_t n) { (void)n; calls_tick++; px_tick += (unsigned long)w * h; for (unsigned j = 0; j < h; j++) for (unsigned i = 0; i < w; i++) if (x + i < 240 && y + j < 240) fb[(y + j) * 240 + x + i] = p[j * w + i]; return 0; }
unsigned fm1_midi_input_source(void) { return 1; }
int groove_master_update(unsigned adc) { (void)adc; return 0; }
void groove_project_poll(void) {}
uint32_t fm1_flash_size(void) { return FLASH_BYTES; }
int fm1_flash_read(uint32_t o, void *p, size_t n) { if (o + n > FLASH_BYTES) return -1; memcpy(p, flash + o, n); return 0; }
int fm1_flash_erase(uint32_t o) { memset(flash + o, 0xff, 4096); return 0; }
int fm1_flash_write(uint32_t o, const void *p, size_t n) { const uint8_t *s = p; for (size_t i = 0; i < n; i++) { if ((flash[o + i] & s[i]) != s[i]) { puts("FLASH WRITE OVER DATA"); exit(1); } flash[o + i] &= s[i]; } return 0; }
const uint8_t *fm1_flash_map(uint32_t o, size_t n) { return o + n <= FLASH_BYTES ? flash + o : NULL; }
static uint8_t last_reply[LF_MAXMSG]; static size_t last_reply_n;
int fm1_midi_send_port(unsigned port, const uint8_t *m, size_t n) { (void)port; memcpy(last_reply, m, n); last_reply_n = n; return 0; }

static int fails;
#define CHECK(c, ...) do { if (c) printf("PASS  "); else { printf("FAIL  "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)
static void ppm(const char *p) {
    FILE *f = fopen(p, "wb"); fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++) { uint16_t c = fb[i]; unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
static void tick(unsigned n) { int32_t blk[256]; while (n--) { ms += 33; for (int i = 0; i < 11; i++) le_render(blk, 128); calls_tick = px_tick = 0; groove_screen_control(); if (calls_tick > calls_max) calls_max = calls_tick; if (px_tick > px_max) px_max = px_tick; calls_total += calls_tick; } }
static int sender_link(void *c, const uint8_t *msg, size_t n, uint8_t *reply, size_t *rn) {
    (void)c; last_reply_n = 0; lofi_sysex(msg, n); tick(1);
    if (last_reply_n) { memcpy(reply, last_reply, last_reply_n); *rn = last_reply_n; } else *rn = 0;
    return 0;
}
static void press_btn(unsigned b) { ctl.buttons = 1u << b; tick(1); ctl.buttons = 0; tick(2); }
static void turn(unsigned enc, int d) { ctl.encoders[enc] = (int16_t)d; tick(1); }
static int led_buttons_on(void) { int n = 0; for (int i = 2; i <= 11; i++) n += led[i]; return n; }
static void shot(const char *name) { tick(60); char p[64]; snprintf(p, sizeof p, "shot_%s.ppm", name); ppm(p); }
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long bytes = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *img = malloc(bytes); if (fread(img, 1, bytes, f) != (size_t)bytes) return 2; fclose(f);
    groove_screen_init(); tick(40); ppm("shot_nopack.ppm");
    lf_sender_t s = {sender_link, 0, 0, 0, 0, 0}; int rc = lf_send_pack(&s, img, (uint32_t)bytes); CHECK(rc == 0 && dev.state == LF_PACK, "upload through the firmware glue (rc %d, %u blocks)", rc, s.sent_blocks);
    ms += 3000; tick(60); le_seq_status_get(&(struct le_seq_status){0}); { struct le_seq_status s0; le_seq_status_get(&s0); CHECK(s0.bpm == 140 && le_get_scale() == 4 && le_get_scale_root() == 0, "defaults: %u bpm, scale %s %u", s0.bpm, le_scale_name(le_get_scale()), le_get_scale_root()); CHECK(le_zone(0)->r >= 105, "default release index %u (about 1 s)", le_zone(0)->r); }
    shot("home"); CHECK(page == PG_HOME && led[8] && led_buttons_on() == 1, "HOME page, only the HOME button lit (page %d, led8 %d, count %d)", page, led[8], led_buttons_on());
    /* every page button lights alone */
    static const struct { unsigned btn, pg; const char *name; } pages[] = {{2, PG_FX, "fx"}, {3, PG_SEL, "sel"}, {4, PG_ENV, "env"}, {5, PG_LFO, "lfo"}, {6, PG_EDIT, "edit"}, {7, PG_GLO, "glo"}, {9, PG_SAVE, "save"}, {10, PG_ARP, "arp"}, {11, PG_SEQ, "seq"}};
    int alone = 1; for (unsigned i = 0; i < 9; i++) { press_btn(pages[i].btn); if (page != pages[i].pg || !led[pages[i].btn] || led_buttons_on() != 1) alone = 0; }
    CHECK(alone, "each page button selects its page and is the only page LED on");
    press_btn(11); CHECK(page == PG_HOME && led[8], "pressing the active page button again returns to HOME");
    /* ENV: edit the active zone */
    press_btn(4); turn(2, 20); turn(3, -15); turn(4, -30); turn(5, 10); CHECK(1, "ENV tiles edit A %d D %d S %d R %d", le_zone(0)->a, le_zone(0)->d, le_zone(0)->s, le_zone(0)->r); shot("env");
    /* LFO */
    press_btn(5); turn(2, 30); turn(3, 40); turn(4, 1); turn(5, 1); shot("lfo"); CHECK(le_zone(0)->lfo_depth == 107 - 67 || le_zone(0)->lfo_depth > 0, "LFO depth %d shape %d target %d", le_zone(0)->lfo_depth, le_zone(0)->lfo_shape, le_zone(0)->lfo_target);
    /* OCT+ switches to the next zone (bank): Tape Key is a melodic zone of 27 keys */
    press_btn(1); CHECK(bank == 1 && az == 9, "OCT+ switches to zone 2 (bank %d, active sample %d)", bank, az);
    { int lit = 0; for (int k = 0; k < 27; k++) lit += led[14 + k]; tick(4); lit = 0; for (int k = 0; k < 27; k++) lit += led[14 + k]; CHECK(lit == 27, "a melodic zone lights all %d keys", lit); }
    press_btn(4 + 0 * 0); press_btn(4); /* ENV then HOME again */ press_btn(8);
    CHECK(le_zone(9)->oct == -1, "a melodic zone starts one octave down (%d)", le_zone(9)->oct); turn(4, 1); CHECK(le_zone(9)->oct == 0, "HOME K3: base octave of the melodic sample is %d", le_zone(9)->oct); turn(4, -1);
    press_btn(6); turn(2, 12); turn(3, -10); turn(4, -20); turn(5, 1); shot("edit");
    CHECK(az == 9 && le_zone(9)->start > 0 && le_zone(9)->end > 0 && le_zone(9)->rev == 1, "EDIT on zone %d: start %u end %u vol %u rev %u", az, le_zone(9)->start, le_zone(9)->end, le_zone(9)->vol, le_zone(9)->rev);
    /* GLO */
    press_btn(7); turn(2, 20); turn(3, -1); turn(4, 2); turn(5, 1); struct le_seq_status st; le_seq_status_get(&st); shot("glo");
    CHECK(st.bpm == 180 && st.beats == 3 && le_get_scale_root() == 1 && le_get_scale() == 5, "GLO: %u bpm, %u beats, root %u, scale %s", st.bpm, st.beats, le_get_scale_root(), le_scale_name(le_get_scale()));
    turn(3, 1); le_seq_status_get(&st); tick(2); le_seq_status_get(&st); turn(4, -2); turn(5, -1);   /* back to C and MIN */
    /* a key that sounds goes dark, back to lit when the hit is over */
    press_btn(8); ctl.keys = 1u << 0; tick(3); CHECK(!led[14 + 0] && led[14 + 1], "a sounding key (kit hit) is dark while its neighbour stays lit");
    ctl.keys = 0; tick(80); CHECK(led[14 + 0], "the LED comes back when the sound is over");
    le_arp()->on = 0; le_note_on(32 + 2, 100); tick(3); CHECK(!led[14 + 2], "a note that does not come from a key (sequencer / MIDI style) is shown too"); le_all_off(); tick(60);
    /* SEL: a key mutes its sample; OCT- goes back to the kit zone */
    press_btn(0); CHECK(bank == 0, "OCT- back to zone 1"); { int lit = 0; for (int k = 0; k < 27; k++) lit += led[14 + k]; tick(4); lit = 0; for (int k = 0; k < 27; k++) lit += led[14 + k]; CHECK(lit == 9, "a kit lights only its %d filled keys", lit); }
    press_btn(3); ctl.keys = 1u << 3; tick(2); ctl.keys = 0; tick(2); shot("sel"); CHECK(le_zone(3)->mute == 1 && !led[14 + 3], "SEL: key 3 muted zone 3 (LED of the key went dark)");
    ctl.keys = 1u << 3; tick(2); ctl.keys = 0; tick(2); CHECK(le_zone(3)->mute == 0 && led[14 + 3], "SEL: pressing it again unmutes");
    /* FX: keys are punch-ins, tiles edit the effects */
    press_btn(2); turn(0, 1); turn(2, 20); turn(3, 10); turn(4, 5); turn(5, 40); ctl.keys = 1u << 1; tick(4); shot("fx"); ctl.keys = 0; tick(2);
    CHECK(fx_sel == 1 && le_fx_params(FX_DELAY)[0] > 40 && le_zone(az)->send[FX_DELAY] > 0, "FX: delay selected, time %u, send of zone %d %u", le_fx_params(FX_DELAY)[0], az, le_zone(az)->send[FX_DELAY]);
    CHECK(led[14 + 11] && !led[14 + 12], "FX: the first 12 keys are lit (punch-ins), the others dark");
    /* ARP */
    press_btn(10); turn(0, 1); CHECK(le_arp()->on == 1, "ARP switched on with SELECT"); turn(2, 1); turn(3, 2); turn(5, 4); ctl.keys = (1u << 0) | (1u << 2) | (1u << 4); tick(40); shot("arp"); ctl.keys = 0; tick(8); turn(0, -1); CHECK(le_arp()->on == 0, "ARP off again");
    le_seq_set_free(0); tick(2);
    /* SEQ + SAVE: record a pattern with a count-in, PLAY while recording only stops the recording */
    press_btn(9); ctl.keys = 1u << 2; tick(2); ctl.keys = 0; tick(2); le_seq_status_get(&st); CHECK(st.pattern == 2, "SAVE: key 2 selects pattern C");
    press_btn(11); turn(2, -1); turn(3, 0); le_seq_status_get(&st);
    press_btn(13); le_seq_status_get(&st); CHECK(st.state == LS_COUNTIN, "REC from stop starts the count-in");
    int guard = 0; while (st.state == LS_COUNTIN && guard++ < 400) { tick(1); le_seq_status_get(&st); } CHECK(st.state == LS_REC, "count-in ends, recording");
    for (int beat = 0; beat < 4; beat++) { ctl.keys = 1u << (beat * 2); tick(2); ctl.keys = 0; tick(7); }
    le_seq_status_get(&st); CHECK(st.events >= 8, "recorded %u events", st.events); shot("seq");
    press_btn(12); le_seq_status_get(&st); CHECK(st.state == LS_PLAY, "PLAY during recording: recording stops, playback goes on (state %u)", st.state);
    press_btn(12); le_seq_status_get(&st); CHECK(st.state == LS_STOP, "PLAY again stops");
    press_btn(12); le_seq_status_get(&st); CHECK(st.state == LS_PLAY, "PLAY starts the loop"); press_btn(13); le_seq_status_get(&st); CHECK(st.state == LS_REC, "REC punches in"); press_btn(13); le_seq_status_get(&st); CHECK(st.state == LS_PLAY, "REC again leaves recording"); press_btn(12);
    /* SAVE + reboot */
    press_btn(9); turn(2, 1); tick(80); shot("save"); CHECK(!unsaved, "SAVE page: SAVE wrote the flash");
    groove_screen_init(); tick(5); { struct le_seq_status s2; tick(5); le_seq_status_get(&s2); (void)s2; }
    tick(5); le_seq_select(2); tick(2); le_seq_status_get(&st);
    CHECK(st.events >= 8 && le_zone(9)->start > 0 && le_zone(0)->a == 40 && le_zone(9)->rev == 1, "after a power cycle: pattern C has %u events, settings of the zones are back (start %u, attack %u)", st.events, le_zone(9)->start, le_zone(0)->a);
    { /* piano roll: repeated notes must be short bars, not one note as long as the loop */
      struct le_seq_event e[8] = {{0, 60, 100, 1}, {6, 60, 0, 0}, {96, 60, 100, 1}, {102, 60, 0, 0}, {192, 60, 100, 1}, {204, 60, 0, 0}, {288, 62, 100, 1}, {380, 62, 0, 0}};
      unsigned l0 = seq_note_len(e, 8, 0, 384), l1 = seq_note_len(e, 8, 2, 384), l2 = seq_note_len(e, 8, 4, 384), l3 = seq_note_len(e, 8, 6, 384);
      CHECK(l0 == 6 && l1 == 6 && l2 == 12 && l3 == 92, "piano roll note lengths: 6 6 12 92 ticks (got %u %u %u %u), not the whole loop", l0, l1, l2, l3); }
    { /* free first take: SEQ page K4 = FREE, REC arms, the clock starts with the first key, REC ends it and the length sets bars + bpm */
      le_seq_stop(); tick(3); le_seq_clear(); tick(3); le_seq_select(3); tick(3); le_seq_set_bars(1); le_seq_set_bpm(120); tick(3); press_btn(11); turn(5, 1); tick(3); le_seq_status_get(&st); CHECK(st.freemode == 1, "SEQ page: K4 switches the first take to FREE");
      press_btn(13); le_seq_status_get(&st); CHECK(st.state == LS_FREE && st.armed, "REC with FREE: armed, no count-in"); shot("free_wait");
      for (int n = 0; n < 8; n++) { ctl.keys = 1u << (n % 4); tick(2); tick(10); ctl.keys = 1u << ((n + 1) % 4); tick(10); }      /* legato notes, each held while the next starts */
      ctl.keys = 0; tick(2); le_seq_status_get(&st); CHECK(st.state == LS_FREE && !st.armed, "the first key started the take"); shot("free_take");
      press_btn(13); tick(3); le_seq_status_get(&st); CHECK(st.state == LS_PLAY && st.events >= 16, "REC ends the free take: loop plays (%u bars, %u bpm, %u events)", st.bars, st.bpm, st.events); shot("free_roll");
      { /* the roll drawn in strips behind the playhead must equal the roll drawn whole (no stale or missing bar pieces) */
        static uint16_t fa[240 * 240]; tick(150); int ph1 = seq_ph; memcpy(fa, fb, sizeof fa); dirty |= 2; tick(3); int ph2 = seq_ph, bad = 0;
        for (int y = 30; y < 148; y++) for (int x = 0; x < 240; x++) if ((x < ph1 - 6 || x > ph1 + 6) && (x < ph2 - 6 || x > ph2 + 6) && fa[y * 240 + x] != fb[y * 240 + x]) bad++;
        CHECK(bad == 0, "SEQ roll: strips drawn while playing equal a full redraw (%d different pixels)", bad); }
      { /* overdub of the same key: press, press, release, release - the lengths are paired by the recording order */
        struct le_seq_event e[4] = {{100, 60, 100, 1}, {150, 60, 0, 0}, {120, 60, 100, 1}, {170, 60, 0, 0}};
        CHECK(seq_note_len(e, 4, 0, 384) == 50 && seq_note_len(e, 4, 2, 384) == 50, "overlapping passes of one key: both notes 50 ticks (got %u %u)", seq_note_len(e, 4, 0, 384), seq_note_len(e, 4, 2, 384)); }
      press_btn(12); tick(3); turn(5, 1); le_seq_set_bpm(120); le_seq_set_bars(2); }
    printf("screen work per control tick: at most %lu calls, %lu pixels (all ticks of the run: %lu calls)\n", calls_max, px_max, calls_total);
    CHECK(calls_max <= 60 && px_max <= 13000, "no single control tick draws more than 60 screen calls / 13000 pixels (it was 385 calls / 92573 pixels)");
    printf("%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
    return fails ? 1 : 0;
}
