#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "xm.h"
#include "audio.h"


u8 xm_sine_table[] =
{
  0,  24,  49,  74,  97, 120, 141, 161,
180, 197, 212, 224, 235, 244, 250, 253,
255, 253, 250, 244, 235, 224, 212, 197,
180, 161, 141, 120,  97,  74,  49,  24
};


XM_player_state_t ps;


u32 XM_NoteToPeriod(u8 note, s8 finetune)
{
    return 7680 - (note * 64) - (finetune / 2);
}

void XM_ResetEnvelopeState(XM_envelope_state_t *env)
{
    if (env) {
        env->frame = 0;
        env->pos = 0;
        env->value = 0;
        env->active = 0;
    }
}

void XM_ResetChannelState(u8 ci)
{
    XM_channel_state_t *channel = &ps.cs[ci];

    channel->period = 0;
    channel->volume = 0x7F;
    channel->panning = 0x80;
    channel->note = 0;
    channel->note_control = XM_NOTE_VOLUME | XM_NOTE_PANNING;
    channel->instrument = 0;

    channel->sample = 0;
    channel->sample_offset = 0;

    channel->fxtype = 0;
    channel->fxparam = 0;

    XM_ResetEnvelopeState(&channel->volume_envelope);
    XM_ResetEnvelopeState(&channel->panning_envelope);
    
    channel->tone_porta_target = 0;
    channel->tone_porta_speed = 0;
    
    channel->vibrato_pos = 0;
    channel->vibrato_speed = 0;
    channel->vibrato_depth = 0;
    channel->vibrato_delta = 0;
    
}


void XM_InitPlayer(XM_module_t *module)
{
    // create frequency table
    ps.linear_frequencies = (u32*)malloc(7681 * sizeof(u32));

    for (int i = 0; i < 7681; i++)
        ps.linear_frequencies[i] = 8363 * pow(2, (4608.0f - i) / 768.0f);

    ps.module = module;
    ps.tick = 0;
    ps.pattern_index = 0;
    ps.row = 0;

    ps.current_bpm = module->default_bpm;
    ps.current_tempo = module->default_tempo;

    ps.global_volume = 64;
    
    ps.cs = (XM_channel_state_t*)malloc(module->num_channels * sizeof(XM_channel_state_t));
 
    for (int i = 0; i < module->num_channels; i++)
        XM_ResetChannelState(i);
}

void XM_PrintNote(u8 ci, XM_note_t *note)
{
    if (note->note)
        printf("%.2d ", note->note);
    else
        printf(".. ");
    
    if (note->instrument)
        printf("%.2X ", note->instrument);
    else
        printf(".. ");
    
    if (note->volume)
        printf("%.2X ", note->volume);
    else
        printf(".. ");
    
    if (note->fxtype != XM_FX_NO_EFFECT)
        printf("%.1X%.2X", note->fxtype, note->fxparam);
    else
        printf("...");
    
    if (ci != ps.module->num_channels-1)
        printf(" | ");    
}

void XM_UpdateChannel(u8 ci)
{
    XM_channel_state_t *channel = &ps.cs[ci];

    // trigger sample
    if (channel->note_control & XM_NOTE_TRIGGER) {
        u8 data_type = channel->sample->type & XM_SAMPLE_16BIT ? 2 : 1;
        
        if (channel->fxtype == XM_FX_SAMPLE_OFFSET)
            S_SetSampleOffset(ci, channel->sample_offset);
        else
            S_SetSampleOffset(ci, 0);
        
        S_PlayVoice(ci, data_type, channel->sample->length, channel->sample->data);
        
        channel->note_control &= ~XM_NOTE_TRIGGER;
    }

    // set sample frequency
    if (channel->note_control & XM_NOTE_FREQ) {
        u16 period = channel->period;
        
        if (channel->fxtype == XM_FX_VIBRATO)
            period += channel->vibrato_delta;
        
        u32 freq = ps.linear_frequencies[channel->period];

        S_SetVoiceFrequency(ci, freq);
        
        channel->note_control &= ~XM_NOTE_FREQ;
    }

    // set channel volume
    if (channel->note_control & XM_NOTE_VOLUME) {
        float final_volume = 1.0f;
        final_volume *= (float)ps.global_volume / 64.0f;
        final_volume *= (float)channel->volume / 64.0f;
        final_volume *= (float)channel->volume_fadeout / 65535.0f;
        
        if (channel->volume_envelope.active)
            final_volume *= (float)channel->volume_envelope.value / 64.0f;
        
        S_SetVoiceVolume(ci, final_volume * 127.0f);
        
        channel->note_control &= ~XM_NOTE_VOLUME;
    }

    // set channel panning
    if (channel->note_control & XM_NOTE_PANNING) {
        u8 pan, envpan, final_pan;
        
        pan = channel->panning;
        
        if (channel->panning_envelope.active)
            envpan = channel->panning_envelope.value;
        else
            envpan = 32;
        
        final_pan = pan + ((envpan-32) * (128-abs(pan-128)) / 32);
        
        S_SetVoicePanning(ci, final_pan);
        
        channel->note_control &= ~XM_NOTE_PANNING;
    }
}

u32 XM_ProcessEnvelope(XM_envelope_t *envelope, XM_envelope_state_t *state)
{
    XM_envelope_point_t *current_point, *next_point;

    state->active = envelope->flags & XM_ENVELOPE_ENABLED;
    
    if (!state->active)
        return 0;

    // handle loop
    if ((envelope->flags & XM_ENVELOPE_LOOP) && (state->pos >= envelope->loop_end)) {
        state->pos = envelope->loop_start;
    }

    current_point = &envelope->points[state->pos];
    
    // abort further processing if last point is reached
    if (state->pos >= envelope->num_points-1) {
        state->pos = envelope->num_points-1;
        state->value = current_point->value;
        return 1;
    }
    
    // also abort processing if the sustain point is reached
    // FIXME: handle keyoff
    if ((envelope->flags & XM_ENVELOPE_SUSTAIN) && (state->pos >= envelope->sustain_point)) {
        state->pos = envelope->sustain_point;
        state->value = current_point->value;
        return 1;
    }
    
    next_point = &envelope->points[state->pos+1];
    
    // update current envelope position
    if (next_point->frame <= state->frame) {
        state->pos++;
        current_point = &envelope->points[state->pos];
        next_point = &envelope->points[state->pos+1];
    }
    
    // interpolate value
    u16 frame_diff = next_point->frame - current_point->frame;
    u16 value_diff = next_point->value - current_point->value;
    u16 t = state->frame - current_point->frame;
    
    state->value = current_point->value + ((value_diff * t) / frame_diff);
    
    // next frame
    state->frame++;
    
    return 1;
}

void XM_ProcessVolumeByte(u8 volbyte, XM_channel_state_t *channel)
{
    // FIXME: process volume byte effects
    if (volbyte >= 0x10 && volbyte <= 0x50) {
        channel->note_control |= XM_NOTE_VOLUME;
        channel->volume = volbyte - 0x10;
    }    
}

void XM_ProcessVolumeFadeout(XM_channel_state_t *channel)
{
    XM_instrument_t *instrument = &ps.module->instruments[channel->instrument];
    
    if ((channel->note_control & XM_NOTE_KEY_OFF) && channel->volume_envelope.active) {
        if (channel->volume_fadeout > instrument->volume_fadeout)
            channel->volume_fadeout -= instrument->volume_fadeout;
        else
            channel->volume_fadeout = 0;
    }    
}

void XM_ProcessEffectByte(XM_channel_state_t *channel)
{
    switch (channel->fxtype) {
        case XM_FX_SET_TEMPO:
            if (channel->fxparam > 0) {
                if (channel->fxparam <= 0x1F)
                    ps.current_tempo = channel->fxparam;
                else
                    ps.current_bpm = channel->fxparam;
            }
            break;
            
        case XM_FX_SET_VOLUME:
            channel->volume = channel->fxparam;
            channel->note_control |= XM_NOTE_VOLUME;
            break;
            
        case XM_FX_SET_GLOBAL_VOLUME:
            ps.global_volume = channel->fxparam;
            break;
            
        case XM_FX_SET_PANNING:
            channel->panning = channel->fxparam;
            channel->note_control |= XM_NOTE_PANNING;
            break;
            
        case XM_FX_PATTERN_BREAK:
            // FIXME: pattern break is not correct (use flag)
            ps.pattern_index++;
            ps.row = channel->fxparam;
            printf("playing pattern %d\n", ps.module->pattern_order[ps.pattern_index]);
            return;
            
        case XM_FX_TONE_PORTA:
            channel->tone_porta_speed = channel->fxparam;
            break;
            
        case XM_FX_SAMPLE_OFFSET:
            if (channel->fxparam > 0)
                channel->sample_offset = channel->fxparam << 8;
            break;
            
        case XM_FX_VIBRATO:
            if (channel->fxparam > 0) {
                channel->vibrato_pos = 0;
                channel->vibrato_speed = channel->fxparam >> 4;
                channel->vibrato_depth = channel->fxparam & 0xF;
            }
            break;
            
            /*
        case XM_FX_MULTI_EFFECT_E:
             switch (channel->fxparam >> 4) {
                 case XM_FX_E_NOTE_DELAY:
                     channel->volume = old_volume;
                     channel->period = old_period;
                     channel->panning = old_panning;
                     channel->note_control = 0;
                     break;
             
                 default:
                     printf("Unhandled multieffect: %.1X%.2X\n", channel->fxtype, channel->fxparam);
                     break;
             }
             break;
             */
            
        case XM_FX_NO_EFFECT:
        case XM_FX_VOLUME_SLIDE:
            break;
            
        default:
            printf("Unhandled effect: %.1X%.2X\n", channel->fxtype, channel->fxparam);
            break;
    }    
}

void XM_UpdateRow()
{
    // get current pattern from order table
    XM_pattern_t *pattern = &ps.module->patterns[ps.module->pattern_order[ps.pattern_index]];
    //XM_pattern_t *pattern = &ps.module->patterns[16];

    // process every channel
    for (int ci = 0; ci < ps.module->num_channels; ci++) {
        XM_note_t *note = 0;
        XM_instrument_t *instrument = 0;
        XM_sample_t *sample = 0;
        XM_channel_state_t *channel = 0;
        u8 tone_porta, note_delayed;

        // get current note
        note = &pattern->data[ci + ps.row * ps.module->num_channels];

        // get channel state
        channel = &ps.cs[ci];

        channel->note_control = 0;
        
        // get effect parameter values
        channel->fxtype = note->fxtype;
        channel->fxparam = note->fxparam;

        // handle key off
        if (note->note == 97 || channel->fxparam == XM_FX_KEY_OFF)
            channel->note_control |= XM_NOTE_KEY_OFF;
        
        // are there any tone portamento effects running?
        tone_porta = (channel->fxtype == XM_FX_TONE_PORTA) || (channel->fxtype == XM_FX_TONE_PORTA_VOLUME_SLIDE);
        // should the note be delayed?
        note_delayed = (channel->fxtype == XM_FX_MULTI_EFFECT_E) && ((channel->fxparam >> 4) == XM_FX_E_NOTE_DELAY);
        
        // grab instrument number
        if (note->instrument && !tone_porta)
            channel->instrument = note->instrument - 1;

        // grab note
        if (note->note && note->note != 97 && !tone_porta) {
            channel->note = note->note - 1;
            channel->volume_fadeout = 65535;
            
            XM_ResetEnvelopeState(&channel->volume_envelope);
            XM_ResetEnvelopeState(&channel->panning_envelope);            
        }

        channel->note_control = 0;
        
        // stop the current sample if invalid instrument
        if (channel->instrument >= ps.module->num_instruments) {
            S_StopVoice(ci);
        } else {
            instrument = &ps.module->instruments[channel->instrument];
            
            // set sample
            sample = &instrument->samples[instrument->sample_numbers[channel->note]];
            channel->sample = sample;
            
            // reset envelopes
            XM_ResetEnvelopeState(&channel->volume_envelope);
            XM_ResetEnvelopeState(&channel->panning_envelope);

            // set default panning and volume, unless the note is delayed
            channel->panning = sample->panning;
            channel->volume = sample->volume;

            if (!note_delayed) {            
                channel->note_control |= XM_NOTE_PANNING;
                channel->note_control |= XM_NOTE_VOLUME;
            }
        }

        // process note
        if (note->note && note->note != 97) {
            u16 period = XM_NoteToPeriod(note->note + sample->relative_note - 1, sample->finetune);
            
            if (!tone_porta) {
                channel->period = period;

                if (!note_delayed) {
                    channel->note_control |= XM_NOTE_FREQ;
                    channel->note_control |= XM_NOTE_TRIGGER;
                }
            } else {
                channel->tone_porta_target = period;
            }
        }

        
        // update envelopes
        if (XM_ProcessEnvelope(&instrument->volume_envelope, &channel->volume_envelope))
            channel->note_control |= XM_NOTE_VOLUME;
        if (XM_ProcessEnvelope(&instrument->volume_envelope, &channel->panning_envelope))
            channel->note_control |= XM_NOTE_PANNING;

        XM_ProcessVolumeFadeout(channel);        
        XM_ProcessVolumeByte(note->volume, channel);
        XM_ProcessEffectByte(channel);

        XM_PrintNote(ci, note);
        
        XM_UpdateChannel(ci);
    }

    ps.row++;
    printf("\n");

    if (ps.row >= pattern->num_rows) {
        ps.pattern_index++;
        ps.row = 0;

        printf("playing pattern %d\n", ps.module->pattern_order[ps.pattern_index]);
    }
}


void XM_FXVolumeSlide(XM_channel_state_t *channel)
{
    u8 param_x, param_y;

    param_x = channel->fxparam >> 4;
    param_y = channel->fxparam & 0xF;

    if (param_x) {
        channel->volume += param_x;
        if (channel->volume > 64)
            channel->volume = 64;
    } else if (param_y) {
        channel->volume -= param_y;
        if (channel->volume < 0)
            channel->volume = 0;
    }

    channel->note_control |= XM_NOTE_VOLUME;
}

void XM_FXTonePorta(XM_channel_state_t *channel)
{
    if (channel->period < channel->tone_porta_target) {
        channel->period += channel->tone_porta_speed << 2;

        if (channel->period > channel->tone_porta_target)
            channel->period = channel->tone_porta_target;
    } else if (channel->period > channel->tone_porta_target) {
        channel->period -= channel->tone_porta_speed << 2;

        if (channel->period < channel->tone_porta_target)
            channel->period = channel->tone_porta_target;
    }

    channel->note_control |= XM_NOTE_FREQ;
}

void XM_FXVibrato(XM_channel_state_t *channel)
{
    s32 delta = xm_sine_table[abs(channel->vibrato_pos)];
    delta *= channel->vibrato_depth;
    delta /= 4;
    
    if (channel->vibrato_pos < 0)
        delta *= -1;
    
    channel->vibrato_delta = delta;
    
    channel->vibrato_pos += channel->vibrato_speed;
    
    if (channel->vibrato_pos > 31)
        channel->vibrato_pos -= 64;
        
    channel->note_control |= XM_NOTE_FREQ;
}

void XM_FXENoteDelay(XM_channel_state_t *channel)
{
    if (ps.tick == (channel->fxparam & 0xF)) {
        channel->note_control |= XM_NOTE_TRIGGER;
        channel->note_control |= XM_NOTE_FREQ;
        channel->note_control |= XM_NOTE_VOLUME;
        channel->note_control |= XM_NOTE_PANNING;
    }
}

void XM_UpdateEffects()
{
    for (int i = 0; i < ps.module->num_channels; i++) {
        XM_channel_state_t *channel = &ps.cs[i];
        XM_instrument_t *instrument = &ps.module->instruments[channel->instrument];
        
        XM_ProcessEnvelope(&instrument->volume_envelope, &channel->volume_envelope);
        XM_ProcessEnvelope(&instrument->panning_envelope, &channel->panning_envelope);

        XM_ProcessVolumeFadeout(channel);
        
        switch (channel->fxtype) {
            case XM_FX_VOLUME_SLIDE:
                XM_FXVolumeSlide(channel);
                break;

            case XM_FX_TONE_PORTA:
                XM_FXTonePorta(channel);
                break;
                
            case XM_FX_VIBRATO:
                XM_FXVibrato(channel);
                break;
        
            case XM_FX_MULTI_EFFECT_E:
                switch (channel->fxparam >> 4) {
                    case XM_FX_E_NOTE_DELAY:
                        XM_FXENoteDelay(channel);
                        break;
                }
        }
        
        XM_UpdateChannel(i);
    }
}



void XM_RunTick()
{
    if (ps.pattern_index >= ps.module->song_length)
        return;

    if (ps.tick % ps.current_tempo == 0)
        XM_UpdateRow();
    else
        XM_UpdateEffects();
    
    ps.tick++;
}

u16 XM_GetCurrentBPM()
{
    return ps.current_bpm;
}
