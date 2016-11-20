#ifndef XM_H
#define XM_H

#include "types.h"


// ----------------------------------------------------------------------------
// XM module structure & loader
// ----------------------------------------------------------------------------

// effect types
#define XM_FX_NO_EFFECT                0xFF
#define XM_FX_ARPEGGIO                 0x0
#define XM_FX_PORTA_UP                 0x1
#define XM_FX_PORTA_DOWN               0x2
#define XM_FX_TONE_PORTA               0x3
#define XM_FX_VIBRATO                  0x4
#define XM_FX_TONE_PORTA_VOLUME_SLIDE  0x5
#define XM_FX_VIBRATO_VOLUME_SLIDE     0x6
#define XM_FX_TREMOLO                  0x7
#define XM_FX_SET_PANNING              0x8
#define XM_FX_SAMPLE_OFFSET            0x9
#define XM_FX_VOLUME_SLIDE             0xA
#define XM_FX_POSITION_JUMP            0xB
#define XM_FX_SET_VOLUME               0xC
#define XM_FX_PATTERN_BREAK            0xD
#define XM_FX_MULTI_EFFECT_E           0xE
#define XM_FX_E_FINE_PORTA_UP          0x1
#define XM_FX_E_FINE_PORTA_DOWN        0x2
#define XM_FX_E_SET_GLISS_CONTROL      0x3
#define XM_FX_E_SET_VIBRATO_CONTROL    0x4
#define XM_FX_E_SET_FINETUNE           0x5
#define XM_FX_E_SET_LOOP_BEGIN         0x6
#define XM_FX_E_SET_TREMOLO_CONTROL    0x7
#define XM_FX_E_RETRIG_NOTE            0x9
#define XM_FX_E_FINE_VOLUME_SLIDE_UP   0xA
#define XM_FX_E_FINE_VOLUME_SLIDE_DOWN 0xB
#define XM_FX_E_NOTE_CUT               0xC
#define XM_FX_E_NOTE_DELAY             0xD
#define XM_FX_E_PATTERN_DELAY          0xE
#define XM_FX_SET_TEMPO                0xF
#define XM_FX_SET_GLOBAL_VOLUME        0x10
#define XM_FX_GLOBAL_VOLUME_SLIDE      0x11
#define XM_FX_KEY_OFF                  0x14
#define XM_FX_SET_ENVELOPE_POS         0x15
#define XM_FX_PANNING_SLIDE            0x18
#define XM_FX_MULTI_RETRIG_NOTE        0x1A
#define XM_FX_TREMOR                   0x1C
#define XM_FX_MULTI_EFFECT_X           0x1F
#define XM_FX_X_EXTRA_FINE_PORTA_UP    0xF1
#define XM_FX_X_EXTRA_FINE_PORTA_DOWN  0xF2


// note and pattern data

typedef struct XM_note_t {
    u8 note;
    u8 instrument;
    u8 volume;
    u8 fxtype;
    u8 fxparam;
} XM_note_t;

typedef struct XM_pattern_t{
    u16 num_rows;
    XM_note_t *data;
} XM_pattern_t;


// samples

#define XM_SAMPLE_FWD_LOOP 0x1
#define XM_SAMPLE_PP_LOOP  0x2
#define XM_SAMPLE_16BIT    0x10

typedef struct XM_sample_t {
    u32 length;
    u32 loop_start;
    u32 loop_length;
    u8 volume;
    u8 finetune;
    u8 type;
    u8 panning;
    s8 relative_note;
    void *data;
} XM_sample_t;


// instrument envelopes

#define XM_MAX_ENVELOPE_POINTS 12

typedef struct {
    u16 frame;
    u16 value;
} XM_envelope_point_t;

#define XM_ENVELOPE_ENABLED 0x1
#define XM_ENVELOPE_SUSTAIN 0x2
#define XM_ENVELOPE_LOOP    0x4

typedef struct {
    u8 num_points;
    u8 sustain_point;
    u8 loop_start;
    u8 loop_end;
    u8 flags;
    XM_envelope_point_t points[XM_MAX_ENVELOPE_POINTS];
} XM_envelope_t;


// instruments

typedef struct XM_instrument_t {
    u8 type;
    
    u16 num_samples;
    u32 sample_header_length;
    u8 sample_numbers[96];

    XM_envelope_t volume_envelope;
    XM_envelope_t panning_envelope;
    
    u8 vibrato_type;
    u8 vibrato_sweep;
    u8 vibrato_depth;
    u8 vibrato_rate;
    
    u16 volume_fadeout;

    XM_sample_t *samples;
} XM_instrument_t;


#define XM_MODULE_LINEAR_FREQ 0x1

typedef struct XM_module_t {
    u16 version;
    u16 song_length;
    u16 song_restart_pos;
    u16 num_channels;
    u16 num_patterns;
    u16 num_instruments;
    u16 flags;
    u16 default_tempo;
    u16 default_bpm;
    u8 pattern_order[256];

    XM_pattern_t *patterns;
    XM_instrument_t *instruments;
} XM_module_t;


s32 XM_LoadFile(const char* file, XM_module_t *module);


// ----------------------------------------------------------------------------
// XM Player
// ----------------------------------------------------------------------------

#define XM_NOTE_TRIGGER 0x1
#define XM_NOTE_STOP    0x2
#define XM_NOTE_FREQ    0x4
#define XM_NOTE_VOLUME  0x8
#define XM_NOTE_PANNING 0x10
#define XM_NOTE_KEY_OFF 0x20

typedef struct {
    u16 frame; // 0..65535
    u8 pos;    // 0..XM_MAX_ENVELOPE_POINTS-1
    u8 value;  // 0..64
    u8 active; // 0..1
} XM_envelope_state_t;

typedef struct XM_channel_state_t {    
    u16 period;  // 0..7681
    s8 volume;   // 0..64
    u8 panning;  // 0..255
    u8 note;     // 0..97
    u8 note_control;

    u8 instrument; // 0..127
    
    XM_sample_t *sample;
    u32 sample_offset;

    u8 fxtype; // 0..31
    u8 fxparam; // 0..255

    XM_envelope_state_t volume_envelope;
    XM_envelope_state_t panning_envelope;
    
    u16 tone_porta_target; // 0..7681
    u8 tone_porta_speed;   // 0..255
    
    s8 vibrato_pos;    // -31..+31
    u8 vibrato_speed;  // 0..15
    u8 vibrato_depth;  // 0..15
    s16 vibrato_delta;
    
    u16 volume_fadeout; // 0..65535
} XM_channel_state_t;

typedef struct XM_player_state_t {
    XM_module_t *module;
    u32 tick;
    u8 pattern_index;
    u16 row;
    u32 *linear_frequencies;
    XM_channel_state_t *cs;
    
    u16 current_bpm;
    u16 current_tempo;
    
    u8 global_volume;
} XM_player_state_t;

void XM_InitPlayer(XM_module_t *module);
void XM_RunTick();

u16 XM_GetCurrentBPM();

u32 XM_NoteToFrequency(u8 note, s8 finetune);

#endif
