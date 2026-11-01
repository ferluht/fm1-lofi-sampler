/* lofi sampler engine: 8 voices, streams 2-bit lofi_codec data straight from (memory mapped) flash, Hermite resampler,
 * live speed from 1/16x to 2x, per zone ADSR / LFO / start / end / reverse / volume / mute / sends, four send effects with punch-ins,
 * arpeggiator, live loop sequencer. Portable C: runs on the FM-1 (audio interrupt) and on the host. */
#ifndef LOFI_ENGINE_H
#define LOFI_ENGINE_H
#include "lofi_pack.h"
#define LE_VOICES 8
#define LE_OUT_RATE 44100
#define LE_SRC_RATE 16000
/* control task side */
void le_init(void);
int le_bind(const uint8_t *pack, uint32_t bytes);   /* 0 ok. Silences everything first and resets the per zone settings. NULL unbinds */
const lfp_info_t *le_info(void);                    /* NULL when no pack */
int le_slot_for_note(unsigned note);                /* -1 none */
void le_set_scale(unsigned id, unsigned root);       /* id 0 off, 1..7 = CHROM MIX MAJ MIN HMIN PHRYG MIN+ ; root = pitch class 0..11 */
unsigned le_get_scale(void); unsigned le_get_scale_root(void);
const char *le_scale_name(unsigned id);
int le_pitch_offset(unsigned slot, unsigned note);   /* semitones relative to the sample's own pitch that key `note` of the zone plays */
void le_slot_on(unsigned slot, unsigned note, unsigned velocity);   /* note picks the pitch relative to the slot's root, also the note-off key */
void le_note_on(unsigned note, unsigned velocity);
void le_note_off(unsigned note);
void le_all_off(void);
void le_set_speed(int steps48);        /* live speed, 1/48 octave steps, clamps to -192 (4 octaves down) .. +48 (1 up) */
int le_get_speed(void);
void le_set_bend(int semitones_x100);  /* pitch bend in 1/100 semitone */
void le_set_level(unsigned level);     /* master 0..127 */
unsigned le_get_level(void);
unsigned le_voices_active(void);
const uint8_t *le_pack(void);          /* the bound pack image */
uint32_t le_sounding_keys(unsigned bank);   /* bit k = a voice of key k of the zone is sounding (key held, or a note of the sequencer / arpeggiator / MIDI) */
uint32_t le_voice_mask(void);          /* bit per zone that has a sounding voice */

/* The note ids of the engine: bank * 32 + key, key 0..26 (a bank is what the panel calls a zone: 27 keys, switched with OCT- / OCT+).
 * A bank holds either a kit (a sample per key, one key zones) or one melodic sample spread over its keys. MIDI notes 0..127 address banks 0..3 directly. */
/* ---- per zone settings (written by the control task, read by the audio side). 0..127 unless noted ---- */
typedef struct {
    uint8_t a, d, s, r;                      /* ADSR: times 1 ms .. 4 s (exponential), sustain level */
    uint8_t lfo_rate, lfo_depth, lfo_shape, lfo_target;   /* rate 0.1..25 Hz; shape 0 sine 1 tri 2 square 3 saw; target 0 pitch 1 volume 2 pan */
    uint8_t vol, mute, rev, send[4];         /* volume 100 = unity; sends to REVERB DELAY CRUSH FILTER */
    int8_t oct;                              /* base octave of a melodic sample, -3..3 */
    uint32_t start, end;                     /* frames; end 0 = to the end of the sample */
} le_zone_t;
le_zone_t *le_zone(unsigned slot);
void le_zone_defaults(unsigned slot);
void le_zone_cut(unsigned slot);        /* silence the voices of a zone now (after muting it) */
enum { FX_REVERB, FX_DELAY, FX_CRUSH, FX_FILTER, FX_COUNT };
/* fx params [fx][0..2] (the 4th tile of the screen is the zone's send): REVERB size damp return | DELAY time feedback tone | CRUSH bits rate return | FILTER cutoff reso type */
uint8_t *le_fx_params(unsigned fx);
/* punch-ins: momentary master effects; id 0..11 */
enum { PN_REVERB, PN_DELAY, PN_CRUSH, PN_DOWN, PN_LPF, PN_HPF, PN_STUT8, PN_STUT16, PN_STUTTER, PN_HALF, PN_DOUBLE, PN_STOP, PN_COUNT };
void le_punch(unsigned id, int on);
/* arpeggiator: when on, keys go through le_arp_note_*; cfg is edited in place */
typedef struct { uint8_t on, rate /*0 1/4, 1 1/8, 2 1/16, 3 1/32*/, dir /*0 up 1 down 2 updown 3 random*/, range /*1..4*/, gate /*5..100 %*/; } le_arp_t;
le_arp_t *le_arp(void);
void le_arp_note_on(unsigned note, unsigned vel);
void le_arp_note_off(unsigned note);
void le_arp_release(void);

/* ---- sequencer: live loop recorder. 96 ticks per beat, up to 8 patterns of 1/2/4/8 bars, overdub, quantize, metronome ---- */
enum { LS_STOP, LS_COUNTIN, LS_PLAY, LS_REC, LS_FREE };   /* LS_FREE: free first take (armed until the first note), its length sets bars and BPM */
enum { LS_PATTERNS = 8, LS_MAX_EVENTS = 160, LS_TICKS_BEAT = 96 };
struct le_seq_status { uint8_t state, pattern, bars, quant, metro, countin, beats, freemode, armed; uint16_t bpm, events; uint32_t tick, loop_ticks; };
struct le_seq_event { uint16_t tick; uint8_t note, vel, on; };
void le_seq_play(void);                 /* from stop: play the selected pattern from its start */
void le_seq_stop(void);                 /* stop and release the sequenced notes */
void le_seq_rec(int on);                /* on: from stop a one bar count-in, then record; while playing: punch in. off: back to play */
void le_seq_clear(void);                /* erase the selected pattern */
void le_seq_clear_all(void);
void le_seq_copy(unsigned to);          /* selected pattern -> pattern `to` */
void le_seq_undo(void);                 /* remove what the last REC pass added */
void le_seq_select(unsigned pattern);   /* 0..7 */
void le_seq_set_bars(unsigned bars);    /* 1, 2, 4, 8 */
void le_seq_set_beats(unsigned beats);  /* beats per bar 2..7 (the measure) */
void le_seq_set_bpm(unsigned bpm);      /* 40..240 */
void le_seq_set_quant(unsigned q);      /* 0 off, 1 = 1/8, 2 = 1/16, 3 = 1/32 */
void le_seq_set_metro(unsigned on, unsigned countin);
void le_seq_set_free(unsigned on);       /* 1: REC from stop records a free first take (no click, starts with the first note); its length becomes the loop and sets the BPM */
void le_seq_status_get(struct le_seq_status *st);
unsigned le_seq_events(unsigned pattern, struct le_seq_event *out, unsigned max);   /* copy of a pattern for drawing */
unsigned le_seq_pattern_count(unsigned pattern);
unsigned le_seq_export(uint8_t *buf, unsigned cap);       /* all patterns + tempo, for the flash; call when not recording. returns bytes */
int le_seq_import(const uint8_t *buf, unsigned n);        /* 0 ok */
unsigned le_settings_export(uint8_t *buf, unsigned cap);  /* zone settings, fx, arp, level, scale of the bound pack */
int le_settings_import(const uint8_t *buf, unsigned n);
/* audio side: adds nothing, writes FM1_BLOCK_FRAMES stereo frames of signed Q23 */
void le_render(int32_t *out, unsigned frames);
#endif
