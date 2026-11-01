/* (documentation: docs/ENGINE.md) */
#include <string.h>
#ifdef FM1_HOST_TEST
#define FM1_AUDIO_CODE
#define FM1_AUDIO_DATA
#else
#include "fm1.h"
#endif
#define LOFI_DQT_ATTR FM1_AUDIO_DATA   /* the decoder table must live in RAM: audio runs while flash is being programmed */
#define LOFI_DECODE_ONLY
#include "lofi_engine.h"
#define RING 64u
#define BLK 128u
enum { EV_ON = 1, EV_OFF, EV_ALLOFF, EV_AON, EV_AOFF, EV_ARELEASE, EV_ZCUT, EV_SEQ = 16 };   /* SEQ commands: type EV_SEQ + n, slot/key/vel/pitch carry the argument */
#define STUT_MAX 16384          /* stutter slice: at most 16384 frames (0.37 s), 64 KB */
typedef struct { uint8_t type, slot, key, vel; int32_t pitch_q8; } event_t;   /* pitch: 1/48 octave steps x 256 relative to the sample */
typedef struct {
    uint8_t active, released, fast, oneshot, loop, rev, key, vel, stage; uint16_t slot;
    const uint8_t *data; uint32_t frames, nsb, start, end, loop_start, loop_end, d;      /* d = samples decoded so far (forward) */
    uint64_t phase; int32_t pitch_q8, target_q8, glide_q8; uint32_t env, a_step, d_step, r_step, sus, age, lfo_phase, lfo_inc;   /* env Q24 */
    int32_t rbase; int32_t state[8]; int16_t ring[RING], rbuf[LOFI_SUPER];                                              /* rbuf: the decoded superblock for reverse play */
} voice_t;
typedef struct { int32_t low, band; } svf_t;
#define RV_C0 1031
#define RV_C1 1117
#define RV_C2 1213
#define RV_C3 1301
#define RV_A0 225
#define RV_A1 341
#define DLY_N 22000
static struct {
    volatile uint8_t bound, scale, scale_root; const uint8_t *pack; uint32_t bytes; lfp_info_t info;
    event_t ev[64]; volatile uint8_t ev_w, ev_r;
    volatile int32_t speed_q16, bend_q16, level_q8; int speed_steps; volatile uint16_t punch;
    voice_t v[LE_VOICES]; uint32_t age; int32_t last_pitch[LFP_SLOTS]; uint32_t last_valid; uint32_t zone_mask;
    le_zone_t zone[LFP_SLOTS]; uint8_t fx[FX_COUNT][4]; le_arp_t arp;
    int32_t punch_speed_q16; uint32_t mclk_acc, mclk;
    int32_t bus[FX_COUNT][BLK]; uint32_t fx_hot[FX_COUNT];
    uint16_t f_tab[128];
    int16_t rv[RV_C0 + RV_C1 + RV_C2 + RV_C3 + RV_A0 + RV_A1]; uint16_t rv_i[6]; int32_t rv_lp[4];
    int16_t dly[DLY_N]; uint32_t dly_w; int32_t dly_lp, dly_hold, dly_acc; uint8_t dly_phase;
    int32_t gate_g, cr_hold; uint32_t st_len, st_pos; uint8_t st_on, st_rec; int16_t st_buf[2 * STUT_MAX]; uint32_t cr_ctr; svf_t flt, pf; int32_t pcr_hold; uint32_t pcr_ctr;
} g;
typedef struct { uint16_t tick; uint8_t note, vel, on, fresh, pass, pad; } sev_t;
static struct {
    volatile uint8_t state, pattern, quant, metro, countin, beats, bars[LS_PATTERNS]; volatile uint16_t bpm, count[LS_PATTERNS];
    sev_t ev[LS_PATTERNS][LS_MAX_EVENTS];
    uint32_t tick, abs, acc, loop_ticks, count_tick, step_q16, tpb; uint8_t pass_id;   /* abs: ticks since the transport started; every pattern plays at abs % its length, so patterns stay in step */
    int8_t off_shift[256]; uint16_t on_tick[256]; uint8_t held[256]; uint8_t open_[256]; uint8_t freemode, armed; int click_pos; uint8_t click_accent;
} q;
static struct { uint8_t n, notes[8]; int pos, dirv; uint32_t acc, tick, rnd; int cur; int off_in; } ar;
/* 2^(i/48) in Q16 */
static FM1_AUDIO_DATA const uint32_t pow48[48] = {
    65536,66471,67415,68370,69336,70313,71301,72300,73310,74332,75366,76412,77470,78541,79624,80719,
    81828,82950,84084,85233,86395,87571,88761,89965,91184,92417,93664,94927,96205,97498,98807,100132,
    101473,102830,104204,105594,107002,108427,109869,111330,112808,114305,115820,117354,118907,120479,122071,123682 };
static FM1_AUDIO_CODE uint32_t exp2_q16(int32_t steps48)   /* 2^(steps/48) in Q16, any sign */
{
    int32_t o = steps48 >= 0 ? steps48 / 48 : -((-steps48 + 47) / 48), r = steps48 - o * 48;
    uint64_t v = pow48[r]; return o >= 0 ? (uint32_t)(v << o) : (uint32_t)(v >> -o);
}
/* 2^(p/(48*256)) in Q16 with linear interpolation between the 1/48 octave steps; audio side */
static FM1_AUDIO_CODE uint32_t exp2_q8(int32_t p)
{
    int32_t st = p >> 8, fr = p & 255; uint32_t a = exp2_q16(st), b = exp2_q16(st + 1);
    return a + (uint32_t)(((int64_t)((int32_t)b - (int32_t)a) * fr) >> 8);
}
static FM1_AUDIO_CODE int32_t clampi(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
static FM1_AUDIO_CODE int32_t sat16(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
static void seq_calc(void);
void le_set_speed(int s) { s = clampi(s, -192, 48); g.speed_steps = s; g.speed_q16 = (int32_t)exp2_q16(s); }
int le_get_speed(void) { return g.speed_steps; }
void le_set_bend(int x100) { g.bend_q16 = (int32_t)exp2_q16(x100 * 4 / 100); }   /* semitone = 4 steps of 1/48 */
void le_set_level(unsigned l) { if (l > 127) l = 127; g.level_q8 = (int32_t)l * 2; }
unsigned le_get_level(void) { return (unsigned)g.level_q8 / 2; }
le_zone_t *le_zone(unsigned slot) { return &g.zone[slot < LFP_SLOTS ? slot : 0]; }
uint8_t *le_fx_params(unsigned fx) { return g.fx[fx < FX_COUNT ? fx : 0]; }
le_arp_t *le_arp(void) { return &g.arp; }
void le_zone_defaults(unsigned i)
{
    le_zone_t *z = &g.zone[i]; memset(z, 0, sizeof *z);
    z->a = 0; z->d = 64; z->s = 127; z->r = 106;   /* release about 1 s */ z->lfo_rate = 40; z->vol = 100; z->rev = (g.info.slot[i].flags & 2) ? 1 : 0;
    z->oct = g.info.slot[i].hi > g.info.slot[i].lo ? -1 : 0;           /* a melodic zone starts one octave below the sample's pitch */
}
static void fx_defaults(void)
{
    static const uint8_t d[FX_COUNT][4] = {{70, 50, 60, 0}, {40, 55, 70, 0}, {70, 30, 90, 0}, {90, 30, 0, 0}};
    memcpy(g.fx, d, sizeof g.fx); g.arp.on = 0; g.arp.rate = 2; g.arp.dir = 0; g.arp.range = 2; g.arp.gate = 60;
}
static void tables_init(void)
{
    for (unsigned i = 0; i < 128; i++) {         /* state variable filter coefficient f = 2 sin(pi fc / fs), fc = 40 Hz .. 10 kHz */
        uint64_t fc_q16 = (uint64_t)exp2_q16((int32_t)(i * 384 / 127)) * 40u;
        int64_t x = (int64_t)((fc_q16 * 152998ull) >> 32), x2 = (x * x) >> 15, x3 = (x2 * x) >> 15, x5 = (x3 * x2) >> 15;
        int64_t s = x - x3 / 6 + x5 / 120; int64_t f = 2 * s; if (f > 29000) f = 29000; g.f_tab[i] = (uint16_t)f;
    }
}
void le_init(void)
{
    memset(&g, 0, sizeof g); memset(&q, 0, sizeof q); memset(&ar, 0, sizeof ar); ar.rnd = 12345; ar.cur = -1;
    q.freemode = 1; q.bpm = 140; q.quant = 2; q.metro = 1; q.countin = 1; q.beats = 4; for (unsigned p = 0; p < LS_PATTERNS; p++) q.bars[p] = 2;
    g.scale = 4; g.scale_root = 0;                      /* C minor until a pack says otherwise */
    g.punch_speed_q16 = 65536; g.gate_g = 32768; seq_calc(); tables_init(); fx_defaults(); le_set_speed(0); le_set_bend(0); le_set_level(100);
}
const lfp_info_t *le_info(void) { return g.bound ? &g.info : NULL; }
static void ev_push(const event_t *e)
{
    uint8_t w = g.ev_w, n = (uint8_t)((w + 1) & 63); if (n == g.ev_r) return;   /* full: drop */
    g.ev[w] = *e;
#ifndef FM1_HOST_TEST
    __asm__ volatile("csync" ::: "memory");
#endif
    g.ev_w = n;
}
int le_bind(const uint8_t *pack, uint32_t bytes)
{
    g.bound = 0; g.ev_r = g.ev_w;
    memset(g.v, 0, sizeof g.v); q.state = LS_STOP; memset((void *)q.count, 0, sizeof q.count); memset(q.held, 0, sizeof q.held); ar.n = 0; ar.cur = -1; g.punch = 0;
    if (!pack) return 0;
    if (lfp_parse(pack, bytes, &g.info)) return -1;
    for (unsigned i = 0; i < LFP_SLOTS; i++) le_zone_defaults(i);
    g.last_valid = 0; g.pack = pack; g.bytes = bytes; le_set_scale(g.info.scale, g.info.scale_root); g.bound = 1; return 0;
}
/* Scales. A zone that spans several keys follows the scale: its first key is the tonic (the sample's pitch moved to the scale root),
 * the next keys are the following scale degrees. A zone of one key (a drum hit) ignores the scale and plays at its own pitch. */
static FM1_AUDIO_DATA const struct { const char *name; uint8_t n, step[8]; } scale_tab[7] = {
    {"CHROM", 12, {0, 1, 2, 3, 4, 5, 6, 7}}, {"MIX", 7, {0, 2, 4, 5, 7, 9, 10, 0}}, {"MAJ", 7, {0, 2, 4, 5, 7, 9, 11, 0}},
    {"MIN", 7, {0, 2, 3, 5, 7, 8, 10, 0}}, {"HMIN", 7, {0, 2, 3, 5, 7, 8, 11, 0}}, {"PHRYG", 7, {0, 1, 3, 5, 7, 8, 10, 0}}, {"MIN+", 8, {0, 2, 3, 5, 7, 8, 10, 11}} };
void le_set_scale(unsigned id, unsigned root) { g.scale = (uint8_t)(id > 7 ? 0 : id); g.scale_root = (uint8_t)(root % 12); }
unsigned le_get_scale(void) { return g.scale; }
unsigned le_get_scale_root(void) { return g.scale_root; }
const char *le_scale_name(unsigned id) { return id == 0 || id > 7 ? "OFF" : scale_tab[id - 1].name; }
/* A melodic zone follows the keyboard of the FM-1, whose first key is F: key 0 is the first note of the scale at or above F (F itself in C minor),
 * then the next notes of the scale; without a scale the keys are chromatic from F. Zone octave 0 = the F nearest to the pitch of the sample;
 * a melodic zone starts at -1 (one octave below). */
enum { KB_FIRST_PC = 5 };
static FM1_AUDIO_CODE unsigned key_semis(unsigned i)             /* semitones above the first key's F for key i of a melodic zone */
{
    const unsigned sc = g.scale ? g.scale : 1, n = scale_tab[sc - 1].n; if (sc == 1) return i;
    uint8_t r[8]; for (unsigned d = 0; d < n; d++) r[d] = (uint8_t)((g.scale_root + scale_tab[sc - 1].step[d] + 12 - KB_FIRST_PC) % 12);
    for (unsigned a = 1; a < n; a++) { uint8_t v = r[a]; unsigned j = a; while (j && r[j - 1] > v) { r[j] = r[j - 1]; j--; } r[j] = v; }      /* sorted from F upwards */
    return 12 * (i / n) + r[i % n];
}
/* pitch of an event in 1/48 octave x 256: the semitone offset of the key + the fine tune (cents) of the sample */
static FM1_AUDIO_CODE int32_t ev_pitch(unsigned slot, unsigned note);
FM1_AUDIO_CODE int le_pitch_offset(unsigned slot, unsigned note)
{
    if (!g.bound || slot >= g.info.n_slots) return 0;
    const lfp_slot_t *s = &g.info.slot[slot];
    if (s->lo == s->hi) return 0;                                   /* one key: a kit hit plays at its own pitch */
    const int octs = 12 * g.zone[slot].oct;                           /* the base octave of the melodic sample (HOME) */
    unsigned i = note >= s->lo ? note - s->lo : 0;
    int shift = (((int)KB_FIRST_PC - (int)(s->root % 12) + 6) % 12 + 12) % 12 - 6;     /* root = the pitch class of the sample: the F nearest to it */
    return shift + (int)key_semis(i) + octs;
}
static FM1_AUDIO_CODE int32_t ev_pitch(unsigned slot, unsigned note)
{
    int32_t q = clampi(le_pitch_offset(slot, note), -60, 60) * 4 * 256;
    if (g.bound && slot < g.info.n_slots) q += g.info.slot[slot].tune * 1024 / 100;      /* 1 semitone = 4 * 256 */
    return q;
}
FM1_AUDIO_CODE int le_slot_for_note(unsigned note)
{
    if (!g.bound) return -1;
    for (unsigned i = 0; i < g.info.n_slots; i++) if (note >= g.info.slot[i].lo && note <= g.info.slot[i].hi) return (int)i;
    return -1;
}
void le_slot_on(unsigned slot, unsigned note, unsigned vel)
{
    if (!g.bound || slot >= g.info.n_slots) return;
    event_t e = {EV_ON, (uint8_t)slot, (uint8_t)note, (uint8_t)(vel > 127 ? 127 : vel), ev_pitch(slot, note)};
    ev_push(&e);
}
void le_note_on(unsigned note, unsigned vel) { int s = le_slot_for_note(note); if (s >= 0) le_slot_on((unsigned)s, note, vel); }
void le_note_off(unsigned note) { event_t e = {EV_OFF, 0, (uint8_t)note, 0, 0}; ev_push(&e); }
void le_all_off(void) { event_t e = {EV_ALLOFF, 0, 0, 0, 0}; ev_push(&e); }
void le_arp_note_on(unsigned note, unsigned vel) { event_t e = {EV_AON, 0, (uint8_t)note, (uint8_t)(vel > 127 ? 127 : vel), 0}; ev_push(&e); }
void le_arp_note_off(unsigned note) { event_t e = {EV_AOFF, 0, (uint8_t)note, 0, 0}; ev_push(&e); }
void le_zone_cut(unsigned slot) { event_t e = {EV_ZCUT, (uint8_t)slot, 0, 0, 0}; ev_push(&e); }
void le_arp_release(void) { event_t e = {EV_ARELEASE, 0, 0, 0, 0}; ev_push(&e); }
void le_punch(unsigned id, int on) { if (id < PN_COUNT) { if (on) g.punch |= (uint16_t)(1u << id); else g.punch &= (uint16_t)~(1u << id); } }
unsigned le_voices_active(void) { unsigned n = 0; for (unsigned i = 0; i < LE_VOICES; i++) n += g.v[i].active; return n; }
uint32_t le_voice_mask(void) { return g.zone_mask; }
uint32_t le_sounding_keys(unsigned bank)
{
    uint32_t m = 0;
    for (unsigned i = 0; i < LE_VOICES; i++) { const voice_t *v = &g.v[i]; if (v->active && (!v->released || v->oneshot) && v->key / 32 == bank && v->key % 32 < 27) m |= 1u << (v->key % 32); }
    return m;
}
const uint8_t *le_pack(void) { return g.bound ? g.pack : 0; }

/* ---------------- audio side: voices ---------------- */
static FM1_AUDIO_CODE void decode_next(voice_t *v)
{
    unsigned sb = v->d / LOFI_SUPER, gi = (v->d % LOFI_SUPER) / LOFI_GROUP;
    if (sb >= v->nsb) { for (unsigned i = 0; i < LOFI_GROUP; i++) v->ring[(v->d + i) & (RING - 1)] = 0; v->d += LOFI_GROUP; return; }
    const uint8_t *s = v->data + sb * LOFI_SUPER_BYTES;
    if (gi == 0) lofi_seed(s, v->state);            /* every superblock restarts from its own seed: identical to running on */
    int16_t pcm[LOFI_GROUP];
    lofi_decode_group(lofi_chunk(s, gi / 16), gi % 16, v->state, pcm);
    for (unsigned i = 0; i < LOFI_GROUP; i++) v->ring[(v->d + i) & (RING - 1)] = pcm[i];
    v->d += LOFI_GROUP;
}
static FM1_AUDIO_CODE void seek(voice_t *v, uint32_t p)    /* position the decoder so that samples p-1.. are available */
{
    uint32_t sb = p / LOFI_SUPER;
    v->d = sb * LOFI_SUPER; memset(v->ring, 0, sizeof v->ring);
    while (v->d < p) decode_next(v);
}
static FM1_AUDIO_CODE int32_t rget(voice_t *v, int32_t i)    /* reverse play: sample i of the sample, decoding a whole superblock when needed */
{
    if (i < 0 || (uint32_t)i >= v->frames) return 0;
    int32_t sb = i / (int32_t)LOFI_SUPER;
    if (v->rbase != sb) { lofi_decode_super(v->data + (uint32_t)sb * LOFI_SUPER_BYTES, v->rbuf); v->rbase = sb; }
    return v->rbuf[i % LOFI_SUPER];
}
static FM1_AUDIO_CODE uint32_t env_ms(unsigned idx) { return exp2_q16((int32_t)(idx * 576 / 127)) >> 16; }     /* 1 ms .. 4096 ms */
static FM1_AUDIO_CODE uint32_t env_step(unsigned idx) { uint32_t ms = env_ms(idx); if (ms < 1) ms = 1; uint32_t s = 380433u / ms; return s ? s : 1; }   /* Q24 per frame */
static FM1_AUDIO_CODE void start_voice(voice_t *v, const event_t *e)
{
    const lfp_slot_t *s = &g.info.slot[e->slot]; const le_zone_t *z = &g.zone[e->slot];
    v->active = 1; v->released = 0; v->slot = e->slot; v->key = e->key; v->vel = e->vel; v->age = ++g.age; v->stage = 0;
    v->oneshot = (s->flags & 4) != 0; v->loop = (s->flags & 1) && s->loop_end != LFP_NO_LOOP; v->rev = z->rev && !v->loop;
    v->data = g.pack + s->offset; v->frames = s->frames; v->nsb = (s->frames + LOFI_SUPER - 1) / LOFI_SUPER; v->loop_start = s->loop_start; v->loop_end = s->loop_end;
    v->start = z->start < s->frames ? z->start : 0; v->end = z->end && z->end <= s->frames ? z->end : s->frames; if (v->end <= v->start + 8) { v->start = 0; v->end = s->frames; }
    v->pitch_q8 = v->target_q8 = e->pitch_q8; v->glide_q8 = 0; v->fast = 0; v->env = 0; v->d = 0; v->rbase = -1;
    v->a_step = env_step(z->a); v->d_step = env_step(z->d); v->r_step = env_step(z->r); v->sus = (uint32_t)z->s * (16777216u / 127u);
    v->lfo_phase = 0; v->lfo_inc = (uint32_t)((exp2_q16((int32_t)(z->lfo_rate * 384 / 127)) / 10u) * 190u);       /* 0.1 .. 25 Hz per block of 128 frames */
    memset(v->ring, 0, sizeof v->ring);
    if (v->rev) v->phase = ((uint64_t)(v->end - 1)) << 32;
    else { v->phase = ((uint64_t)v->start) << 32; if (v->start) seek(v, v->start); }
}
static FM1_AUDIO_CODE void cut(voice_t *v) { v->released = 1; v->fast = 1; }
static FM1_AUDIO_CODE void seq_record(const event_t *e, int on);
static FM1_AUDIO_CODE void seq_cmd(const event_t *e);
static FM1_AUDIO_CODE void arp_cmd(const event_t *e);
static FM1_AUDIO_CODE void do_on(const event_t *e)
{
    voice_t *pick = NULL; const lfp_slot_t *z = &g.info.slot[e->slot]; int legato = 0;
    if (g.zone[e->slot].mute) return;                                  /* a muted zone stays silent */
    if (z->choke) for (unsigned i = 0; i < LE_VOICES; i++) if (g.v[i].active && g.info.slot[g.v[i].slot].choke == z->choke) cut(&g.v[i]);     /* choke group: the open hat dies under the closed one */
    if (z->glide) for (unsigned i = 0; i < LE_VOICES; i++) {
        voice_t *v = &g.v[i];
        if (v->active && !v->released && v->slot == e->slot) {        /* legato: slide the sounding voice to the new key, no retrigger */
            unsigned blocks = z->glide * 10U * LE_OUT_RATE / 1000U / 128U; if (blocks < 1) blocks = 1;
            v->target_q8 = e->pitch_q8; v->key = e->key; v->age = ++g.age; int32_t d = e->pitch_q8 - v->pitch_q8; v->glide_q8 = d / (int32_t)blocks; if (!v->glide_q8) v->glide_q8 = d > 0 ? 1 : -1;
            legato = 1; break; }
    }
    if (!legato) {
        if (z->voices) {                                                  /* zone polyphony: steal the oldest voice of this zone when it is full */
            unsigned n = 0; voice_t *old = NULL;
            for (unsigned i = 0; i < LE_VOICES; i++) if (g.v[i].active && !g.v[i].fast && g.v[i].slot == e->slot) { n++; if (!old || g.v[i].age < old->age) old = &g.v[i]; }
            if (n >= z->voices && old) cut(old);
        }
        for (unsigned i = 0; i < LE_VOICES; i++) if (!g.v[i].active) { pick = &g.v[i]; break; }
        if (!pick) { uint32_t best = 0xffffffffu; for (unsigned i = 0; i < LE_VOICES; i++) { uint32_t k = g.v[i].released ? g.v[i].age : g.v[i].age + 0x80000000u; if (k < best) { best = k; pick = &g.v[i]; } } }
        start_voice(pick, e);
        if (z->glide && (g.last_valid >> e->slot & 1U)) {                  /* glide in from the last pitch this zone played */
            unsigned blocks = z->glide * 10U * LE_OUT_RATE / 1000U / 128U; if (blocks < 1) blocks = 1;
            pick->pitch_q8 = g.last_pitch[e->slot]; int32_t d = pick->target_q8 - pick->pitch_q8; pick->glide_q8 = d / (int32_t)blocks; if (!pick->glide_q8 && d) pick->glide_q8 = d > 0 ? 1 : -1; }
    }
    g.last_pitch[e->slot] = e->pitch_q8; g.last_valid |= 1U << e->slot;
}
static FM1_AUDIO_CODE void do_off(unsigned key) { for (unsigned i = 0; i < LE_VOICES; i++) if (g.v[i].active && g.v[i].key == key && !g.v[i].oneshot) g.v[i].released = 1; }
static FM1_AUDIO_CODE void handle_events(void)
{
    while (g.ev_r != g.ev_w) {
        const event_t *e = &g.ev[g.ev_r];
        if (e->type >= EV_SEQ) seq_cmd(e);
        else if (e->type == EV_ON) { do_on(e); seq_record(e, 1); }
        else if (e->type == EV_OFF) { do_off(e->key); seq_record(e, 0); }
        else if (e->type >= EV_AON && e->type <= EV_ARELEASE) arp_cmd(e);
        else if (e->type == EV_ZCUT) { for (unsigned i = 0; i < LE_VOICES; i++) if (g.v[i].active && g.v[i].slot == e->slot) cut(&g.v[i]); }
        else for (unsigned i = 0; i < LE_VOICES; i++) g.v[i].released = g.v[i].active;
        g.ev_r = (uint8_t)((g.ev_r + 1) & 63);
    }
}
static FM1_AUDIO_CODE int32_t hermite(int32_t y0, int32_t y1, int32_t y2, int32_t y3, int32_t t /* Q15 */)
{
    int32_t c1 = (y2 - y0) >> 1, c2 = y0 - ((5 * y1 - 4 * y2 + y3) >> 1), c3 = ((y3 - y0) >> 1) + ((3 * (y1 - y2)) >> 1);
    int64_t r = ((int64_t)c3 * t) >> 15; r = ((r + c2) * t) >> 15; r = ((r + c1) * t) >> 15;
    return (int32_t)(r + y1);
}
/* ---------------- sequencer (runs in the audio interrupt, block accurate) ---------------- */
enum { SC_PLAY = EV_SEQ, SC_STOP, SC_REC, SC_CLEAR, SC_UNDO, SC_SELECT, SC_BARS, SC_BPM, SC_QUANT, SC_METRO, SC_CLEARALL, SC_COPY, SC_BEATS, SC_FREEMODE };
static FM1_AUDIO_CODE unsigned grid(void) { static FM1_AUDIO_DATA const uint8_t g_[4] = {1, 48, 24, 12}; return g_[q.quant & 3]; }
static void seq_calc(void)
{
    q.tpb = (uint32_t)q.beats * LS_TICKS_BEAT; q.loop_ticks = (uint32_t)q.bars[q.pattern] * q.tpb;
    q.step_q16 = (uint32_t)(((uint64_t)q.bpm * LS_TICKS_BEAT << 16) / (60ull * LE_OUT_RATE));       /* ticks per output frame, Q16 */
}
static FM1_AUDIO_CODE void seq_release_all(void) { for (unsigned i = 0; i < 256; i++) if (q.held[i]) { do_off(i); q.held[i] = 0; } }
static FM1_AUDIO_CODE void play_note(unsigned note, unsigned vel, int on)    /* a note of the arpeggiator: sounds and is recorded like a played key */
{
    if (on) {
        int s = le_slot_for_note(note); if (s < 0) return;
        event_t ev = {EV_ON, (uint8_t)s, (uint8_t)note, (uint8_t)vel, ev_pitch((unsigned)s, note)}; do_on(&ev); seq_record(&ev, 1);
    } else { event_t ev = {EV_OFF, 0, (uint8_t)note, 0, 0}; do_off(note); seq_record(&ev, 0); }
}
static FM1_AUDIO_CODE void seq_fire(unsigned tick)
{
    sev_t *e = q.ev[q.pattern];
    for (unsigned i = 0; i < q.count[q.pattern]; i++) if (e[i].tick == tick && !e[i].fresh) {
        if (e[i].on) {
            int s = le_slot_for_note(e[i].note); if (s < 0) continue;
            event_t ev = {EV_ON, (uint8_t)s, e[i].note, e[i].vel, ev_pitch((unsigned)s, e[i].note)};
            do_on(&ev); q.held[e[i].note] = 1;
        } else { do_off(e[i].note); q.held[e[i].note] = 0; }
    }
}
static FM1_AUDIO_CODE void seq_wrap(void)
{
    sev_t *e = q.ev[q.pattern]; for (unsigned i = 0; i < q.count[q.pattern]; i++) e[i].fresh = 0;     /* what was just played in is part of the loop from now on */
}
static FM1_AUDIO_CODE void seq_record(const event_t *e, int on)
{
    if ((q.state != LS_REC && q.state != LS_FREE) || !g.bound) return;
    if (q.state == LS_FREE && q.armed) { if (!on) return; q.armed = 0; q.tick = 0; q.abs = 0; q.acc = 0; }      /* free loop: the clock starts with the first note */
    unsigned p = q.pattern, gr = grid(), t = q.tick, qt = t;
    if (on && q.state == LS_FREE) qt = t;                    /* raw time, no grid: the tempo is not known yet */
    else if (on) { qt = ((t + gr / 2) / gr) * gr; if (qt >= q.loop_ticks) qt = 0; }
    else {                                                  /* the release keeps the length of the played note, at least 6 ticks */
        int sh = q.off_shift[e->key], v = (int)t + sh, L = (int)q.loop_ticks; qt = (unsigned)(((v % L) + L) % L);
        int len = ((int)qt - (int)q.on_tick[e->key] + L) % L; if (len < 6) qt = (unsigned)(((int)q.on_tick[e->key] + 6) % L); }
    if (!on && !q.open_[e->key]) return;                     /* a release whose press was not recorded in this pass (key held before REC): nothing to close */
    if (on) { unsigned no = 0; for (unsigned k = 0; k < 256; k++) no += q.open_[k]; if (q.count[p] + no + 2 > LS_MAX_EVENTS) return; }   /* keep room for the release of every open note */
    else if (q.count[p] >= LS_MAX_EVENTS) return;
    q.open_[e->key] = (uint8_t)on;
    if (on) { q.off_shift[e->key] = (int8_t)clampi((int)qt - (int)t, -127, 127); q.on_tick[e->key] = (uint16_t)qt; }
    sev_t *s = &q.ev[p][q.count[p]]; s->tick = (uint16_t)qt; s->note = e->key; s->vel = e->vel; s->on = (uint8_t)on; s->fresh = 1; s->pass = q.pass_id;
    q.count[p]++;
    if (on) q.held[e->key] = 0;           /* a live note: its release is recorded, not played back */
}
static FM1_AUDIO_CODE void seq_close_open(void)     /* recording ends (or the pattern changes) with keys still down: release them in the pattern now */
{
    if (q.state == LS_REC || (q.state == LS_FREE && !q.armed)) for (unsigned k = 0; k < 256; k++) if (q.open_[k]) { event_t ev = {EV_OFF, 0, (uint8_t)k, 0, 0}; seq_record(&ev, 0); }
    memset(q.open_, 0, sizeof q.open_);
}
static FM1_AUDIO_CODE void seq_free_finish(void)     /* the free take ends: its length becomes the loop, bars and BPM follow from it */
{
    unsigned p = q.pattern, E = q.tick; sev_t *e = q.ev[p];
    if (q.armed || E < 48) { memset(q.open_, 0, sizeof q.open_); q.count[p] = 0; q.armed = 0; q.state = LS_STOP; seq_calc(); seq_release_all(); return; }   /* nothing played (or under half a beat) */
    seq_close_open();
    static const uint8_t opts[4] = {1, 2, 4, 8}; unsigned bars = 1, best = ~0u, bpm = 120; uint64_t base = (uint64_t)q.beats * q.bpm * LS_TICKS_BEAT;   /* bpm = bars * beats * bpm0 * 96 / E */
    for (unsigned i = 0; i < 4; i++) {
        unsigned b = (unsigned)((opts[i] * base + E / 2) / E); if (b < 40 || b > 240) continue;
        unsigned sc = b > 120 ? b * 100 / 120 : 12000 / b; if (sc < best) { best = sc; bars = opts[i]; bpm = b; }
    }
    if (best == ~0u) { bars = 4; bpm = (unsigned)((4 * base + E / 2) / E); bpm = bpm < 40 ? 40 : bpm > 240 ? 240 : bpm; }
    unsigned L = bars * q.beats * LS_TICKS_BEAT;
    for (unsigned i = 0; i < q.count[p]; i++) { uint32_t t = (uint32_t)((uint64_t)e[i].tick * L / E); e[i].tick = (uint16_t)(t >= L ? L - 1 : t); e[i].fresh = 0; }
    q.bars[p] = (uint8_t)bars; q.bpm = (uint16_t)bpm; seq_calc();
    q.state = LS_PLAY; q.tick = 0; q.abs = 0; q.acc = 0; memset(q.held, 0, sizeof q.held); seq_release_all();
}
static FM1_AUDIO_CODE void seq_cmd(const event_t *e)
{
    switch (e->type) {
    case SC_PLAY: if (q.state == LS_STOP) { seq_calc(); q.state = LS_PLAY; q.tick = 0; q.abs = 0; q.acc = 0; } break;
    case SC_STOP: if (q.state == LS_FREE) { seq_free_finish(); if (q.state == LS_PLAY) q.state = LS_STOP; break; } seq_close_open(); q.state = LS_STOP; seq_release_all(); break;
    case SC_REC:
        if (e->key) {
            if (q.state == LS_STOP && q.freemode) { seq_calc(); q.pass_id++; q.count[q.pattern] = 0; memset(q.open_, 0, sizeof q.open_); q.loop_ticks = 60000; q.state = LS_FREE; q.armed = 1; q.tick = 0; q.abs = 0; q.acc = 0; }
            else if (q.state == LS_STOP) { seq_calc(); q.pass_id++; if (q.countin) { q.state = LS_COUNTIN; q.count_tick = 0; q.tick = 0; q.abs = 0; q.acc = 0; q.click_pos = 0; } else { q.state = LS_REC; q.tick = 0; q.abs = 0; q.acc = 0; } }
            else if (q.state == LS_PLAY) { q.pass_id++; q.state = LS_REC; }
        } else if (q.state == LS_FREE) seq_free_finish();
        else if (q.state == LS_REC) { seq_close_open(); q.state = LS_PLAY; }
        else if (q.state == LS_COUNTIN) q.state = LS_STOP;
        break;
    case SC_CLEAR: if (q.state == LS_FREE) break; memset(q.open_, 0, sizeof q.open_); q.count[q.pattern] = 0; seq_release_all(); break;
    case SC_CLEARALL: if (q.state == LS_FREE) break; memset(q.open_, 0, sizeof q.open_); for (unsigned p = 0; p < LS_PATTERNS; p++) q.count[p] = 0; seq_release_all(); break;
    case SC_COPY: if (e->key < LS_PATTERNS && e->key != q.pattern) { unsigned to = e->key; memcpy(q.ev[to], q.ev[q.pattern], sizeof q.ev[to]); q.count[to] = q.count[q.pattern]; q.bars[to] = q.bars[q.pattern]; for (unsigned i = 0; i < q.count[to]; i++) q.ev[to][i].fresh = 0; } break;
    case SC_UNDO: { memset(q.open_, 0, sizeof q.open_); unsigned n = 0; sev_t *s = q.ev[q.pattern]; for (unsigned i = 0; i < q.count[q.pattern]; i++) if (s[i].pass != q.pass_id) s[n++] = s[i]; q.count[q.pattern] = (uint16_t)n; if (q.pass_id) q.pass_id--; seq_release_all(); } break;
    case SC_SELECT: if (e->key < LS_PATTERNS && q.state != LS_FREE) { int was = q.state; seq_close_open(); if (was != LS_STOP) { seq_release_all(); if (was == LS_COUNTIN) q.state = LS_PLAY; } q.pattern = e->key; seq_calc(); if (was != LS_STOP) q.tick = q.abs % q.loop_ticks; } break;   /* keep the position on the shared clock */
    case SC_BARS: if (q.state == LS_FREE) break; seq_close_open(); q.bars[q.pattern] = e->key; seq_calc(); { sev_t *s = q.ev[q.pattern]; unsigned n = 0; for (unsigned i = 0; i < q.count[q.pattern]; i++) if (s[i].tick < q.loop_ticks) s[n++] = s[i]; q.count[q.pattern] = (uint16_t)n; } q.tick = q.abs % q.loop_ticks; break;
    case SC_BEATS: if (e->key >= 2 && e->key <= 7 && q.state == LS_STOP) { for (unsigned p = 0; p < LS_PATTERNS; p++) q.count[p] = 0; q.beats = e->key; seq_calc(); } break;   /* a new measure invalidates the recorded loops */
    case SC_BPM: if (q.state == LS_FREE) break; q.bpm = (uint16_t)(e->key | e->vel << 8); seq_calc(); break;
    case SC_QUANT: q.quant = e->key; break;
    case SC_FREEMODE: q.freemode = e->key ? 1 : 0; break;
    case SC_METRO: q.metro = e->key; q.countin = e->vel; break;
    default: break;
    }
}
static FM1_AUDIO_CODE void arp_step(void)
{
    if (ar.cur >= 0) { play_note((unsigned)ar.cur, 0, 0); ar.cur = -1; }
    if (!ar.n) return;
    unsigned range = g.arp.range < 1 ? 1 : g.arp.range > 4 ? 4 : g.arp.range, total = ar.n * range;
    if (ar.pos < 0) ar.pos = g.arp.dir == 1 ? (int)total - 1 : -1;                 /* first step of a new chord */
    switch (g.arp.dir) {
    case 1: ar.pos = (ar.pos + (int)total - 1) % (int)total; break;
    case 2: if (total == 1) ar.pos = 0; else { if (ar.pos < 0) { ar.pos = 0; ar.dirv = 1; } else { ar.pos += ar.dirv; if (ar.pos >= (int)total - 1) { ar.pos = (int)total - 1; ar.dirv = -1; } else if (ar.pos <= 0) { ar.pos = 0; ar.dirv = 1; } } } break;
    case 3: ar.rnd = ar.rnd * 1664525u + 1013904223u; ar.pos = (int)((ar.rnd >> 16) % total); break;
    default: ar.pos = (ar.pos + 1) % (int)total; break;
    }
    unsigned ostep = g.scale ? scale_tab[g.scale - 1].n : 12;                      /* an octave in keys of a melodic zone */
    int base = ar.notes[ar.pos % ar.n], note = base + (int)ostep * (ar.pos / (int)ar.n); if (note > 255 || le_slot_for_note((unsigned)note) < 0) note = base;
    play_note((unsigned)note, 100, 1); ar.cur = note;
    static FM1_AUDIO_DATA const uint8_t st[4] = {96, 48, 24, 12}; unsigned gate = st[g.arp.rate & 3] * (g.arp.gate < 5 ? 5 : g.arp.gate) / 100; ar.off_in = gate < 1 ? 1 : (int)gate;
}
static FM1_AUDIO_CODE void arp_cmd(const event_t *e)
{
    if (e->type == EV_AON) {
        for (unsigned i = 0; i < ar.n; i++) if (ar.notes[i] == e->key) return;
        if (ar.n >= 8) return; if (!ar.n) { ar.tick = 0; ar.acc = 0; ar.pos = -1; ar.dirv = 1; }
        unsigned i = ar.n++; while (i && ar.notes[i - 1] > e->key) { ar.notes[i] = ar.notes[i - 1]; i--; } ar.notes[i] = e->key;
    } else if (e->type == EV_AOFF) {
        for (unsigned i = 0; i < ar.n; i++) if (ar.notes[i] == e->key) { for (unsigned j = i; j + 1 < ar.n; j++) ar.notes[j] = ar.notes[j + 1]; ar.n--; break; }
        if (!ar.n && ar.cur >= 0) { play_note((unsigned)ar.cur, 0, 0); ar.cur = -1; }
    } else { ar.n = 0; if (ar.cur >= 0) { play_note((unsigned)ar.cur, 0, 0); ar.cur = -1; } }
}
static FM1_AUDIO_CODE void arp_run(unsigned frames)
{
    if (!g.arp.on || (!ar.n && ar.cur < 0)) return;
    static FM1_AUDIO_DATA const uint8_t st[4] = {96, 48, 24, 12};
    for (unsigned f = 0; f < frames; f++) {
        ar.acc += q.step_q16;
        while (ar.acc >= 65536u) {
            ar.acc -= 65536u;
            if (ar.tick % st[g.arp.rate & 3] == 0) arp_step();
            else if (ar.cur >= 0 && ar.off_in > 0 && --ar.off_in == 0) { play_note((unsigned)ar.cur, 0, 0); ar.cur = -1; }
            ar.tick++;
        }
    }
}
static FM1_AUDIO_CODE void seq_run(unsigned frames, int32_t *out)
{
    if (q.state == LS_STOP || (q.state == LS_FREE && q.armed)) return;
    for (unsigned f = 0; f < frames; f++) {
        q.acc += q.step_q16;
        while (q.acc >= 65536u) {
            q.acc -= 65536u;
            if (q.state == LS_COUNTIN) {
                if (q.count_tick % LS_TICKS_BEAT == 0) { q.click_pos = 0; q.click_accent = q.count_tick == 0; }
                if (++q.count_tick >= q.tpb) { q.state = LS_REC; q.tick = 0; q.abs = 0; }
            } else {
                seq_fire(q.tick);
                if (q.metro && q.state == LS_REC && q.tick % LS_TICKS_BEAT == 0) { q.click_pos = 0; q.click_accent = q.tick % q.tpb == 0; }
                ++q.abs; if (q.state == LS_FREE) { if (q.tick >= 59990) { seq_free_finish(); break; } ++q.tick; continue; }
                if (++q.tick >= q.loop_ticks) { q.tick = 0; seq_wrap(); }
            }
        }
        if (q.click_pos >= 0 && q.click_pos < 1800 && ((q.metro && q.state == LS_REC) || q.state == LS_COUNTIN)) {       /* metronome: 40 ms decaying square click */
            int32_t env = (1800 - q.click_pos) * 2000 / 1800 * (q.click_accent ? 3 : 2), per = q.click_accent ? 22 : 33;
            int32_t v = ((q.click_pos / (per / 2)) & 1) ? env * 512 : -env * 512; out[2 * f] += v; out[2 * f + 1] += v; q.click_pos++;
        }
    }
}
/* control side */
static void seq_ev(unsigned type, unsigned a, unsigned b) { event_t e = {(uint8_t)type, 0, (uint8_t)a, (uint8_t)b, 0}; ev_push(&e); }
void le_seq_play(void) { seq_ev(SC_PLAY, 0, 0); }
void le_seq_stop(void) { seq_ev(SC_STOP, 0, 0); }
void le_seq_rec(int on) { seq_ev(SC_REC, on ? 1 : 0, 0); }
void le_seq_clear(void) { seq_ev(SC_CLEAR, 0, 0); }
void le_seq_clear_all(void) { seq_ev(SC_CLEARALL, 0, 0); }
void le_seq_copy(unsigned to) { seq_ev(SC_COPY, to, 0); }
void le_seq_undo(void) { seq_ev(SC_UNDO, 0, 0); }
void le_seq_select(unsigned p) { seq_ev(SC_SELECT, p, 0); }
void le_seq_set_bars(unsigned b) { seq_ev(SC_BARS, b == 1 || b == 2 || b == 4 || b == 8 ? b : 2, 0); }
void le_seq_set_beats(unsigned b) { seq_ev(SC_BEATS, b < 2 ? 2 : b > 7 ? 7 : b, 0); }
void le_seq_set_bpm(unsigned bpm) { bpm = bpm < 40 ? 40 : bpm > 240 ? 240 : bpm; seq_ev(SC_BPM, bpm & 255, bpm >> 8); }
void le_seq_set_free(unsigned on) { seq_ev(SC_FREEMODE, on ? 1 : 0, 0); }
void le_seq_set_quant(unsigned qn) { seq_ev(SC_QUANT, qn & 3, 0); }
void le_seq_set_metro(unsigned on, unsigned ci) { seq_ev(SC_METRO, on ? 1 : 0, ci ? 1 : 0); }
void le_seq_status_get(struct le_seq_status *st)
{
    st->state = q.state; st->pattern = q.pattern; st->bars = q.bars[q.pattern]; st->quant = q.quant; st->metro = q.metro; st->countin = q.countin; st->beats = q.beats; st->freemode = q.freemode; st->armed = q.armed;
    st->bpm = q.bpm; st->events = q.count[q.pattern]; st->tick = q.state == LS_COUNTIN ? q.count_tick : q.tick; st->loop_ticks = q.state == LS_COUNTIN ? q.tpb : q.loop_ticks;
}
unsigned le_seq_pattern_count(unsigned p) { return p < LS_PATTERNS ? q.count[p] : 0; }
unsigned le_seq_events(unsigned p, struct le_seq_event *out, unsigned max)
{
    if (p >= LS_PATTERNS) return 0; unsigned n = q.count[p] < max ? q.count[p] : max;
    for (unsigned i = 0; i < n; i++) { out[i].tick = q.ev[p][i].tick; out[i].note = q.ev[p][i].note; out[i].vel = q.ev[p][i].vel; out[i].on = q.ev[p][i].on; }
    return n;
}
/* persistence: 'LFS2', bpm u16, quant u8, metro u8, countin u8, beats u8, then per pattern: bars u8, count u16, events (tick u16, note u8, vel u8 | on<<7, pad) */
unsigned le_seq_export(uint8_t *b, unsigned cap)
{
    unsigned o = 0; if (cap < 16) return 0;
    b[o++] = 'L'; b[o++] = 'F'; b[o++] = 'S'; b[o++] = '2'; b[o++] = (uint8_t)q.bpm; b[o++] = (uint8_t)(q.bpm >> 8); b[o++] = q.quant; b[o++] = q.metro; b[o++] = q.countin; b[o++] = q.beats;
    for (unsigned p = 0; p < LS_PATTERNS; p++) {
        unsigned n = q.count[p]; if (o + 3 + n * 5 > cap) n = (cap - o - 3) / 5;
        b[o++] = q.bars[p]; b[o++] = (uint8_t)n; b[o++] = (uint8_t)(n >> 8);
        for (unsigned i = 0; i < n; i++) { const sev_t *e = &q.ev[p][i]; b[o++] = (uint8_t)e->tick; b[o++] = (uint8_t)(e->tick >> 8); b[o++] = e->note; b[o++] = (uint8_t)((e->vel & 127) | e->on << 7); b[o++] = 0; }
    }
    return o;
}
int le_seq_import(const uint8_t *b, unsigned n)
{
    if (n < 10 || b[0] != 'L' || b[1] != 'F' || b[2] != 'S' || b[3] != '2') return -1;
    unsigned o = 10; uint16_t cnt[LS_PATTERNS]; uint8_t bars[LS_PATTERNS];
    unsigned pos = o; for (unsigned p = 0; p < LS_PATTERNS; p++) { if (pos + 3 > n) return -2; bars[p] = b[pos]; cnt[p] = (uint16_t)(b[pos + 1] | b[pos + 2] << 8); if (cnt[p] > LS_MAX_EVENTS || (bars[p] != 1 && bars[p] != 2 && bars[p] != 4 && bars[p] != 8)) return -3; pos += 3 + cnt[p] * 5; if (pos > n) return -2; }
    if (b[9] < 2 || b[9] > 7) return -4;
    q.state = LS_STOP; q.bpm = (uint16_t)(b[4] | b[5] << 8); q.quant = b[6] & 3; q.metro = b[7] & 1; q.countin = b[8] & 1; q.beats = b[9]; if (q.bpm < 40 || q.bpm > 240) q.bpm = 140;
    pos = o;
    for (unsigned p = 0; p < LS_PATTERNS; p++) { q.bars[p] = bars[p]; q.count[p] = cnt[p]; pos += 3; for (unsigned i = 0; i < cnt[p]; i++, pos += 5) { sev_t *e = &q.ev[p][i]; e->tick = (uint16_t)(b[pos] | b[pos + 1] << 8); e->note = b[pos + 2]; e->vel = b[pos + 3] & 127; e->on = b[pos + 3] >> 7; e->fresh = 0; e->pass = 0; } }
    q.pattern = 0; seq_calc(); return 0;
}
/* ---------------- effects ---------------- */
static FM1_AUDIO_DATA const uint16_t rv_len[6] = {RV_C0, RV_C1, RV_C2, RV_C3, RV_A0, RV_A1};
static FM1_AUDIO_CODE int32_t rv_tick(int32_t x16)       /* four damped combs and two allpasses (Freeverb in small): size = feedback, damp = lowpass in the feedback */
{
    int32_t fb = 24000 + g.fx[FX_REVERB][0] * 5800 / 127, damp = 3000 + g.fx[FX_REVERB][1] * 20000 / 127, in = x16 >> 2, sum = 0; uint32_t off = 0;
    for (unsigned c = 0; c < 4; c++) {
        int16_t *b = g.rv + off; uint16_t i = g.rv_i[c]; int32_t y = b[i];
        g.rv_lp[c] = (int32_t)(((int64_t)g.rv_lp[c] * damp + (int64_t)y * (32768 - damp)) >> 15);
        b[i] = (int16_t)sat16(in + (int32_t)(((int64_t)g.rv_lp[c] * fb) >> 15));
        if (++g.rv_i[c] >= rv_len[c]) g.rv_i[c] = 0; sum += y; off += rv_len[c];
    }
    for (unsigned a = 4; a < 6; a++) {
        int16_t *b = g.rv + off; uint16_t i = g.rv_i[a]; int32_t y = b[i], o = y - sum;
        b[i] = (int16_t)sat16(sum + (y >> 1)); if (++g.rv_i[a] >= rv_len[a]) g.rv_i[a] = 0; sum = o; off += rv_len[a];
    }
    return sat16(sum);
}
static FM1_AUDIO_CODE int32_t dly_tick(int32_t x16, uint32_t n)    /* mono delay at half the sample rate: a lo-fi tape-ish echo */
{
    g.dly_acc += x16; g.dly_phase ^= 1;
    if (g.dly_phase) return g.dly_hold;
    int32_t in = g.dly_acc >> 1; g.dly_acc = 0;
    uint32_t r = (g.dly_w + DLY_N - n) % DLY_N; int32_t y = g.dly[r];
    int32_t fb = 4000 + g.fx[FX_DELAY][1] * 25000 / 127, alpha = 4000 + g.fx[FX_DELAY][2] * 28000 / 127, fv = (int32_t)(((int64_t)y * fb) >> 15);
    g.dly_lp += (int32_t)(((int64_t)(fv - g.dly_lp) * alpha) >> 15);
    g.dly[g.dly_w] = (int16_t)sat16(in + g.dly_lp); if (++g.dly_w >= DLY_N) g.dly_w = 0;
    g.dly_hold = y; return y;
}
static FM1_AUDIO_CODE int32_t crush_tick(int32_t x16, unsigned bits, unsigned rate, int32_t *hold, uint32_t *ctr)
{
    if (++*ctr >= rate) { *ctr = 0; unsigned sh = 16 - bits; *hold = sh ? ((x16 + (1 << (sh - 1))) >> sh) << sh : x16; if (*hold > 32767) *hold = 32767; }
    return *hold;
}
static FM1_AUDIO_CODE int32_t svf_tick(svf_t *s, int32_t x, int32_t f, int32_t qd, unsigned type)
{
    s->low += (int32_t)(((int64_t)f * s->band) >> 15);
    int32_t high = x - s->low - (int32_t)(((int64_t)qd * s->band) >> 15);
    s->band += (int32_t)(((int64_t)f * high) >> 15);
    s->low = clampi(s->low, -(1 << 20), 1 << 20); s->band = clampi(s->band, -(1 << 20), 1 << 20);
    return type == 0 ? s->low : type == 1 ? high : s->band;
}
static FM1_AUDIO_CODE void fx_run(int32_t *out, unsigned frames)
{
    static FM1_AUDIO_DATA const uint8_t beats_x4[8] = {1, 2, 3, 4, 6, 8, 12, 16};      /* delay times in 16th notes */
    const unsigned pn = g.punch;
    /* punch-in throws feed the buses from the dry mix */
    if (pn & (1u << PN_REVERB | 1u << PN_DELAY)) for (unsigned f = 0; f < frames; f++) { int32_t m = (out[2 * f] + out[2 * f + 1]) >> 1; if (pn & (1u << PN_REVERB)) g.bus[FX_REVERB][f] += m; if (pn & (1u << PN_DELAY)) g.bus[FX_DELAY][f] += m; }
    uint32_t dn = (uint32_t)((uint64_t)beats_x4[g.fx[FX_DELAY][0] * 8 / 128] * 60u * 22050u / (4u * (q.bpm ? q.bpm : 100u))); if (dn >= DLY_N) dn = DLY_N - 1; if (dn < 8) dn = 8;
    for (unsigned fx = 0; fx < FX_COUNT; fx++) {
        int32_t peak = 0; for (unsigned f = 0; f < frames; f++) { int32_t a = g.bus[fx][f] < 0 ? -g.bus[fx][f] : g.bus[fx][f]; if (a > peak) peak = a; }
        if (peak > 4096) g.fx_hot[fx] = 3 * LE_OUT_RATE; else if (g.fx_hot[fx] > frames) g.fx_hot[fx] -= frames; else g.fx_hot[fx] = 0;
    }
    int32_t ret_rv = g.fx[FX_REVERB][2] * 3 / 2 + 1, ret_cr = g.fx[FX_CRUSH][2] + 1; unsigned bits = 2 + g.fx[FX_CRUSH][0] * 14 / 127, rate = 1 + g.fx[FX_CRUSH][1] * 31 / 127;
    int32_t ff = g.f_tab[g.fx[FX_FILTER][0] & 127], fq = 60000 - g.fx[FX_FILTER][1] * 54000 / 127; unsigned ft = g.fx[FX_FILTER][2] < 43 ? 0 : g.fx[FX_FILTER][2] < 86 ? 1 : 2;
    for (unsigned f = 0; f < frames; f++) {
        int32_t w = 0;
        if (g.fx_hot[FX_REVERB]) w += (rv_tick(sat16(g.bus[FX_REVERB][f] >> 8)) * ret_rv * 256) >> 7;
        if (g.fx_hot[FX_DELAY]) w += dly_tick(sat16(g.bus[FX_DELAY][f] >> 8), dn) * 256;
        if (g.fx_hot[FX_CRUSH]) w += (crush_tick(sat16(g.bus[FX_CRUSH][f] >> 8), bits, rate, &g.cr_hold, &g.cr_ctr) * ret_cr * 256) >> 7;
        if (g.fx_hot[FX_FILTER]) w += svf_tick(&g.flt, sat16(g.bus[FX_FILTER][f] >> 8), ff, fq, ft) * 256;
        out[2 * f] += w; out[2 * f + 1] += w;
    }
}
static FM1_AUDIO_CODE void punch_master(int32_t *out, unsigned frames)
{
    const unsigned pn = g.punch;
    if (!(pn & (1u << PN_CRUSH | 1u << PN_DOWN | 1u << PN_LPF | 1u << PN_HPF | 1u << PN_STUT8 | 1u << PN_STUT16 | 1u << PN_STUTTER | 1u << PN_STOP)) && g.punch_speed_q16 >= 65000) return;
    uint32_t period = (pn & (1u << PN_STUT16)) ? 24 : (pn & (1u << PN_STUT8)) ? 48 : 0, open = period / 2;
    int32_t gate_t = !period || (g.mclk % period) < open ? 32768 : 0;
    int32_t fade = g.punch_speed_q16 < 16384 ? (g.punch_speed_q16 - 4096) * 32768 / 12288 : 32768; if (fade < 0) fade = 0;
    for (unsigned f = 0; f < frames; f++) {
        g.gate_g += (gate_t - g.gate_g) >> 5;
        if (pn & (1u << PN_STUTTER)) {      /* beat repeat: the first 1/16 note after the press passes through and is recorded, then it loops until the key is released */
            if (!g.st_on) { uint32_t l = q.bpm ? 661500u / q.bpm : 5512u; g.st_len = l < 256 ? 256 : l > STUT_MAX ? STUT_MAX : l; g.st_pos = 0; g.st_rec = 1; g.st_on = 1; }
        } else g.st_on = 0;
        for (unsigned c = 0; c < 2; c++) {
            int32_t x = sat16(out[2 * f + c] >> 8);
            if (g.st_on) {
                int16_t *sb = &g.st_buf[2 * g.st_pos + c];
                if (g.st_rec) *sb = (int16_t)x;
                else { uint32_t e0 = g.st_pos < g.st_len - 1 - g.st_pos ? g.st_pos : g.st_len - 1 - g.st_pos; x = *sb; if (e0 < 32) x = x * (int32_t)(e0 + 1) / 33; }       /* 32 frame fades at the loop edges: no click */
            }
            if (pn & (1u << PN_CRUSH)) x = crush_tick(x, 5, 3, &g.pcr_hold, &g.pcr_ctr); else if (pn & (1u << PN_DOWN)) x = crush_tick(x, 12, 9, &g.pcr_hold, &g.pcr_ctr);
            if (pn & (1u << PN_LPF)) x = svf_tick(&g.pf, x, g.f_tab[34], 30000, 0); else if (pn & (1u << PN_HPF)) x = svf_tick(&g.pf, x, g.f_tab[88], 30000, 1);
            out[2 * f + c] = (int32_t)(((int64_t)x * 256 * g.gate_g >> 15) * fade >> 15);
        }
        if (g.st_on && ++g.st_pos >= g.st_len) { g.st_pos = 0; g.st_rec = 0; }
    }
}
/* ---------------- render ---------------- */
static FM1_AUDIO_CODE int32_t lfo_eval(uint32_t phase, unsigned shape)      /* -32768..32768 */
{
    int32_t t = (int32_t)(phase >> 16);
    switch (shape) {
    case 1: { int32_t x = t - 32768; if (x < 0) x = -x; return (x - 16384) * 2; }
    case 2: return t < 32768 ? 32767 : -32767;
    case 3: return t - 32768;
    default: { int32_t x = t & 0x7fff, y = (x * (32768 - x)) >> 13; return t < 32768 ? y : -y; }
    }
}
static FM1_AUDIO_CODE void render_voice(voice_t *v, unsigned frames, int32_t *out, int64_t live)
{
    const le_zone_t *z = &g.zone[v->slot];
    if (v->pitch_q8 != v->target_q8) { v->pitch_q8 += v->glide_q8; if ((v->glide_q8 > 0 && v->pitch_q8 >= v->target_q8) || (v->glide_q8 < 0 && v->pitch_q8 <= v->target_q8)) v->pitch_q8 = v->target_q8; }
    int32_t pitch = v->pitch_q8, volg = 32768, gl = 32768, gr = 32768, lv = lfo_eval(v->lfo_phase, z->lfo_shape), depth = z->lfo_depth;
    v->lfo_phase += v->lfo_inc;
    if (depth) {
        if (z->lfo_target == 0) pitch += (int32_t)(((int64_t)lv * depth * 12288) / (32768 * 127));
        else if (z->lfo_target == 1) volg = 32768 - (int32_t)((int64_t)depth * (32768 - ((lv + 32768) >> 1)) / 127);
        else { int32_t p = (int32_t)((int64_t)lv * depth / 127); if (p > 0) gl = 32768 - p; else gr = 32768 + p; }
    }
    int64_t r = ((int64_t)exp2_q8(pitch) * live) >> 16;
    if (r < 4096) r = 4096; if (r > 524288) r = 524288;                       /* 1/16x .. 8x of the stored 16 kHz: a melodic zone of 27 keys reaches three octaves above its first key */
    uint64_t step = ((uint64_t)r * LE_SRC_RATE << 16) / LE_OUT_RATE;          /* Q32 source samples per output frame: r * 16000/44100 (exact: no drift) */
    int32_t tg = (int32_t)((int64_t)v->vel * g.level_q8 * z->vol / 100);
    const int32_t s0 = z->send[0], s1 = z->send[1], s2 = z->send[2], s3 = z->send[3];
    for (unsigned f = 0; f < frames; f++) {
        int32_t s;
        if (v->rev) {
            int32_t i = (int32_t)(v->phase >> 32); uint32_t t = (uint32_t)((v->phase >> 17) & 0x7fff);
            if (v->phase < step + ((uint64_t)v->start << 32)) { v->active = 0; break; }
            s = hermite(rget(v, i - 1), rget(v, i), rget(v, i + 1), rget(v, i + 2), (int32_t)t);
            v->phase -= step;
        } else {
            uint32_t pos = (uint32_t)(v->phase >> 32);
            if (v->loop && pos >= v->loop_end) { pos = v->loop_start + (pos - v->loop_end); v->phase = ((uint64_t)pos << 32) | (v->phase & 0xffffffffu); seek(v, pos); }
            if (pos >= v->end) { v->active = 0; break; }
            while (v->d < pos + 3) decode_next(v);
            int32_t t = (int32_t)((v->phase >> 17) & 0x7fff);
            int32_t y0 = pos ? v->ring[(pos - 1) & (RING - 1)] : 0, y1 = v->ring[pos & (RING - 1)], y2 = v->ring[(pos + 1) & (RING - 1)], y3 = v->ring[(pos + 2) & (RING - 1)];
            s = hermite(y0, y1, y2, y3, t);
            v->phase += step;
        }
        if (v->released) { uint32_t rs = v->fast ? 70000u : v->r_step; if (v->env <= rs) { v->active = 0; break; } v->env -= rs; }
        else if (v->stage == 0) { v->env += v->a_step; if (v->env >= 16777216u) { v->env = 16777216u; v->stage = 1; } }
        else if (v->stage == 1) { if (v->env > v->sus + v->d_step) v->env -= v->d_step; else { v->env = v->sus; v->stage = 2; } }
        int32_t a = (int32_t)(((int64_t)s * (int32_t)(v->env >> 8)) >> 16);
        int32_t o = (int32_t)(((int64_t)a * tg) >> 14); o <<= 7; o = (int32_t)(((int64_t)o * volg) >> 15);
        out[2 * f] += (int32_t)(((int64_t)o * gl) >> 15); out[2 * f + 1] += (int32_t)(((int64_t)o * gr) >> 15);
        if (s0) g.bus[FX_REVERB][f] += (o * s0) >> 7; if (s1) g.bus[FX_DELAY][f] += (o * s1) >> 7; if (s2) g.bus[FX_CRUSH][f] += (o * s2) >> 7; if (s3) g.bus[FX_FILTER][f] += (o * s3) >> 7;
    }
}
FM1_AUDIO_CODE void le_render(int32_t *out, unsigned frames)
{
    if (frames > BLK) frames = BLK;
    for (unsigned i = 0; i < frames * 2; i++) out[i] = 0;
    if (!g.bound) { g.ev_r = g.ev_w; return; }
    handle_events();
    arp_run(frames);
    seq_run(frames, out);
    g.mclk_acc += q.step_q16 * frames; g.mclk += g.mclk_acc >> 16; g.mclk_acc &= 0xffffu;       /* free running tick clock for the stutter */
    {   /* punch speeds (half, double, tape stop) glide */
        int32_t target = (g.punch & (1u << PN_STOP)) ? 4096 : (g.punch & (1u << PN_HALF)) ? 32768 : (g.punch & (1u << PN_DOUBLE)) ? 131072 : 65536;
        g.punch_speed_q16 += (target - g.punch_speed_q16) >> ((g.punch & (1u << PN_STOP)) ? 5 : 2);
        if (g.punch_speed_q16 > 65000 && g.punch_speed_q16 < 66100 && target == 65536) g.punch_speed_q16 = 65536;
    }
    memset(g.bus, 0, sizeof g.bus);
    int64_t live = ((int64_t)g.speed_q16 * g.bend_q16) >> 16; live = (live * g.punch_speed_q16) >> 16;
    uint32_t mask = 0;
    for (unsigned n = 0; n < LE_VOICES; n++) { voice_t *v = &g.v[n]; if (!v->active) continue; render_voice(v, frames, out, live); if (v->active) mask |= 1u << v->slot; }
    g.zone_mask = mask;
    fx_run(out, frames);
    punch_master(out, frames);
    for (unsigned i = 0; i < frames * 2; i++) {                 /* soft knee above 75% of full scale instead of hard clipping */
        int32_t x = out[i], a = x < 0 ? -x : x;
        if (a > 6291456) { int32_t e = a - 6291456; if (e > 4194304) e = 4194304; a = 6291456 + e - (int32_t)(((int64_t)e * e) >> 24); if (a > 8388607) a = 8388607; }
        out[i] = x < 0 ? -a : a;
    }
}
/* ---------------- settings of the bound pack: zone controls, fx, arp, master ---------------- */
unsigned le_settings_export(uint8_t *b, unsigned cap)
{
    unsigned n = g.bound ? g.info.n_slots : 0, o = 0; if (cap < 6 + n * 24 + 32) return 0;
    b[o++] = 'L'; b[o++] = 'F'; b[o++] = 'S'; b[o++] = 'T'; b[o++] = 2; b[o++] = (uint8_t)n;
    for (unsigned i = 0; i < n; i++) {
        const le_zone_t *z = &g.zone[i];
        b[o++] = z->a; b[o++] = z->d; b[o++] = z->s; b[o++] = z->r; b[o++] = z->lfo_rate; b[o++] = z->lfo_depth; b[o++] = z->lfo_shape; b[o++] = z->lfo_target;
        b[o++] = z->vol; b[o++] = z->mute; b[o++] = z->rev; b[o++] = (uint8_t)z->oct; b[o++] = z->send[0]; b[o++] = z->send[1]; b[o++] = z->send[2]; b[o++] = z->send[3];
        for (unsigned k = 0; k < 4; k++) b[o++] = (uint8_t)(z->start >> (8 * k)); for (unsigned k = 0; k < 4; k++) b[o++] = (uint8_t)(z->end >> (8 * k));
    }
    for (unsigned f = 0; f < FX_COUNT; f++) for (unsigned k = 0; k < 4; k++) b[o++] = g.fx[f][k];
    b[o++] = g.arp.on; b[o++] = g.arp.rate; b[o++] = g.arp.dir; b[o++] = g.arp.range; b[o++] = g.arp.gate; b[o++] = (uint8_t)le_get_level(); b[o++] = g.scale; b[o++] = g.scale_root;
    return o;
}
int le_settings_import(const uint8_t *b, unsigned len)
{
    if (len < 6 || b[0] != 'L' || b[1] != 'F' || b[2] != 'S' || b[3] != 'T' || b[4] != 2 || !g.bound) return -1;
    unsigned n = b[5], o = 6; if (n != g.info.n_slots || len < o + n * 24 + 24) return -2;
    for (unsigned i = 0; i < n; i++) {
        le_zone_t *z = &g.zone[i];
        z->a = b[o++] & 127; z->d = b[o++] & 127; z->s = b[o++] & 127; z->r = b[o++] & 127; z->lfo_rate = b[o++] & 127; z->lfo_depth = b[o++] & 127; z->lfo_shape = b[o++] & 3; z->lfo_target = b[o++] % 3;
        z->vol = b[o++] & 127; z->mute = b[o++] & 1; z->rev = b[o++] & 1; z->oct = (int8_t)clampi((int8_t)b[o++], -3, 3); z->send[0] = b[o++] & 127; z->send[1] = b[o++] & 127; z->send[2] = b[o++] & 127; z->send[3] = b[o++] & 127;
        z->start = 0; z->end = 0; for (unsigned k = 0; k < 4; k++) z->start |= (uint32_t)b[o++] << (8 * k); for (unsigned k = 0; k < 4; k++) z->end |= (uint32_t)b[o++] << (8 * k);
        if (z->start >= g.info.slot[i].frames) z->start = 0; if (z->end > g.info.slot[i].frames) z->end = 0;
    }
    for (unsigned f = 0; f < FX_COUNT; f++) for (unsigned k = 0; k < 4; k++) g.fx[f][k] = b[o++] & 127;
    g.arp.on = b[o++] & 1; g.arp.rate = b[o++] & 3; g.arp.dir = b[o++] & 3; g.arp.range = b[o] < 1 || b[o] > 4 ? 2 : b[o]; o++; g.arp.gate = b[o] < 5 || b[o] > 100 ? 60 : b[o]; o++;
    le_set_level(b[o++]); le_set_scale(b[o] > 7 ? 0 : b[o], b[o + 1] % 12); return 0;
}
