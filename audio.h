/*
 *  audio.h
 *  ca_test
 *
 *  Created by Oliver Markovic on 14.04.08.
 *  Copyright 2008 Oliver Markovic. All rights reserved.
 *
 */

#ifndef AUDIO_H
#define AUDIO_H

#include "types.h"

int S_Init(u8 num_voices, u32 rate);
void S_Shutdown();

void S_PlayVoice(u8 voice, u8 data_type, u32 length, void *data);
void S_StopVoice(u8 voice);

void S_SetVoiceVolume(u8 voice, u8 vol);
void S_SetVoicePanning(u8 voice, u8 panning);
void S_SetVoiceFrequency(u8 voice, u32 freq);
void S_SetSampleOffset(u8 voice, u32 offset);

#define S_LOOP_NONE 0x0
#define S_LOOP_FWD 0x1
#define S_LOOP_PP 0x2

void S_SetSampleLoop(u8 voice, u8 type, u32 start, u32 end);


#endif
