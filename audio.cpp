/*
 *  audio.cpp
 *  ca_test
 *
 *  Created by Oliver Markovic on 14.04.08.
 *  Copyright 2008 Oliver Markovic. All rights reserved.
 *
 */

#include <AudioToolbox/AUGraph.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreServices/CoreServices.h>

#include "audio.h"

struct s_voice_state
{
    AUNode varispeed_node;
    AudioUnit au_varispeed;
    
    u8 sample_format;
    u32 sample_pos;
    u32 sample_length;
    u8 sample_loop_type;
    u32 sample_loop_start;
    u32 sample_loop_end;
    void *sample_data;
};

struct s_soundsystem_state
{
    AUGraph au_graph;
    
    AUNode mixer_node;
    AudioUnit au_mixer;
    
    AUNode output_node;
    AudioUnit au_output;
    
    int mixing_rate;
    
    int num_voices;
    struct s_voice_state *voices;
} ss;



// ---------------------------------------------------------------------------

OSStatus S_RenderAudioCallback(void *inRefCon, 
                               AudioUnitRenderActionFlags *ioActionFlags, 
                               const AudioTimeStamp *inTimeStamp, 
                               UInt32 inBusNumber, 
                               UInt32 inNumberFrames, 
                               AudioBufferList *ioData)
{
    float *buffer = (float*)ioData->mBuffers[0].mData;

    s_voice_state *voice = &ss.voices[(long)inRefCon];
    
    if (!voice->sample_data) {
        memset(buffer, 0, sizeof(float) * inNumberFrames);
    } else {
        for (int k = 0; k < inNumberFrames; k++) {
            if (voice->sample_loop_type && voice->sample_pos > voice->sample_loop_end) {
                voice->sample_pos = voice->sample_loop_start;
            }
        
            if (voice->sample_pos < voice->sample_length) {
                if (voice->sample_format == 1) {
                    s8* sample_buffer = (s8*)voice->sample_data;
                    buffer[k] = (float)sample_buffer[voice->sample_pos] / 127.0f;
                } else if (voice->sample_format == 2) {
                    s16* sample_buffer = (s16*)voice->sample_data;
                    buffer[k] = (float)sample_buffer[voice->sample_pos] / 32767.0f;
                }
            
                voice->sample_pos++;
            } else {
                buffer[k] = 0.0f;
            }
        }
    }

    // copy data from channel 0 into the other channels
    for (int i = 1; i < ioData->mNumberBuffers; i++) {
        memcpy(ioData->mBuffers[i].mData, ioData->mBuffers[0].mData, ioData->mBuffers[0].mDataByteSize);
        ioData->mBuffers[i].mDataByteSize = ioData->mBuffers[0].mDataByteSize;
    }
    
    return noErr;
}


// ---------------------------------------------------------------------------

void S_CreateAUGraph()
{
    NewAUGraph(&ss.au_graph);
    
    // create varispeed units
    ComponentDescription variCD;
    variCD.componentFlags = 0;
    variCD.componentFlagsMask = 0;
    variCD.componentType = kAudioUnitType_FormatConverter;
    variCD.componentSubType = kAudioUnitSubType_Varispeed;
    variCD.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    for (int i = 0; i < ss.num_voices; i++) {
        AUGraphNewNode(ss.au_graph, &variCD, 0, 0, &ss.voices[i].varispeed_node);
    }
    
    // create mixer unit
    ComponentDescription mixerCD;
    mixerCD.componentFlags = 0;
    mixerCD.componentFlagsMask = 0;
    mixerCD.componentType = kAudioUnitType_Mixer;
    mixerCD.componentSubType = kAudioUnitSubType_StereoMixer;
    mixerCD.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    AUGraphNewNode(ss.au_graph, &mixerCD, 0, 0, &ss.mixer_node);
    
    
    // create output unit
    ComponentDescription outputCD;
    outputCD.componentFlags = 0;
    outputCD.componentFlagsMask = 0;
    outputCD.componentType = kAudioUnitType_Output;
    outputCD.componentSubType = kAudioUnitSubType_DefaultOutput;
    outputCD.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    AUGraphNewNode(ss.au_graph, &outputCD, 0, 0, &ss.output_node);
    
    
    // open the graph and get the audio units
    AUGraphOpen(ss.au_graph);
    AUGraphGetNodeInfo(ss.au_graph, ss.mixer_node, 0, 0, 0, &ss.au_mixer);
    AUGraphGetNodeInfo(ss.au_graph, ss.output_node, 0, 0, 0, &ss.au_output);
    
    for (int i = 0; i < ss.num_voices; i++)
        AUGraphGetNodeInfo(ss.au_graph, ss.voices[i].varispeed_node, 0, 0, 0, &ss.voices[i].au_varispeed);
}

int S_SetMixerBusCount(u8 count)
{
    UInt32 size = sizeof(UInt32);
    UInt32 busCount = count;
    
    OSStatus result = noErr;
    
    result = AudioUnitSetProperty(ss.au_mixer,
                                  kAudioUnitProperty_BusCount,
                                  kAudioUnitScope_Input,
                                  0,
                                  &busCount,
                                  size);
    
    if (result) {
        printf("S_SetMixerBusCount: %4.4s\n", (char*)&result);
        return 1;
    }
    
    return 0;
}

int S_SetStreamFormat(u32 rate)
{
    // set the stream format to native float, stereo
    AudioStreamBasicDescription format;
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = 4;
    format.mBytesPerPacket = 4;
    format.mFramesPerPacket = 1;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    format.mSampleRate = rate;
    format.mChannelsPerFrame = 2;
    
    OSStatus result = noErr;
    
    // setup output unit
    result = AudioUnitSetProperty(ss.au_output,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  0,
                                  &format,
                                  sizeof(AudioStreamBasicDescription));
    
    if (result) {
        printf("S_SetStreamFormat: Couldn't set output format (%4.4s)\n", (char*)&result);
        return 1;
    }
    
    // setup mixer unit
    result = AudioUnitSetProperty(ss.au_mixer,
                         kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Output,
                         0,
                         &format,
                         sizeof(AudioStreamBasicDescription));
    
    if (result) {
        printf("S_SetStreamFormat: Couldn't set mixer format (%4.4s)\n", (char*)&result);
        return 1;
    }
    
    return 0;
}

int S_SetRenderCallback()
{
    // setup callbacks
    AURenderCallbackStruct callback;
    callback.inputProc = S_RenderAudioCallback;
    callback.inputProcRefCon = 0;
    
    for (int i = 0; i < ss.num_voices; i++) {
        OSStatus result = noErr;
    
        callback.inputProcRefCon = (void*)i;
        
        result = AudioUnitSetProperty(ss.voices[i].au_varispeed,
                                      kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Input,
                                      0,
                                      &callback,
                                      sizeof(callback));
    
        if (result) {
            printf("S_SetRenderCallback: %4.4s\n", (char*)&result);
            return 1;
        }
    }
    
    return 0;
}

void S_StartAUGraph()
{
    for (int i = 0; i < ss.num_voices; i++)
        AUGraphConnectNodeInput(ss.au_graph, ss.voices[i].varispeed_node, 0, ss.mixer_node, i);
    
    AUGraphConnectNodeInput(ss.au_graph, ss.mixer_node, 0, ss.output_node, 0);
    
    AUGraphInitialize(ss.au_graph);
    AUGraphStart(ss.au_graph);
    
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, false);
}

// ---------------------------------------------------------------------------

int S_Init(u8 num_voices, u32 rate)
{
    ss.mixing_rate = rate;
    
    ss.num_voices = num_voices;
    ss.voices = (struct s_voice_state*)malloc(sizeof(struct s_voice_state) * num_voices);

    for (int i = 0; i < num_voices; i++) {
        ss.voices[i].sample_pos = 0;
        ss.voices[i].sample_length = 0;
        ss.voices[i].sample_data = 0;
    }
    
    
    S_CreateAUGraph();
    
    if (S_SetMixerBusCount(num_voices))
        return 1;
    
    if (S_SetStreamFormat(rate))
        return 1;
    
    if (S_SetRenderCallback())
        return 1;
    
    S_StartAUGraph();
    
    return 0;
}

void S_Shutdown()
{
    AUGraphStop(ss.au_graph);
    DisposeAUGraph(ss.au_graph);
}


void S_PlayVoice(u8 voice, u8 data_type, u32 length, void *data)
{
    if (voice > ss.num_voices)
        return;
    
    ss.voices[voice].sample_pos = 0;
    ss.voices[voice].sample_format = data_type;
    ss.voices[voice].sample_length = length;
    ss.voices[voice].sample_data = data;
}

void S_StopVoice(u8 voice)
{
    if (voice > ss.num_voices)
        return;
    
    ss.voices[voice].sample_pos = 0;
    ss.voices[voice].sample_length = 0;
    ss.voices[voice].sample_data = 0;
}


void S_SetVoiceVolume(u8 voice, u8 vol)
{
    if (voice > ss.num_voices)
        return;

    OSStatus err = noErr;

    err = AudioUnitSetParameter(ss.au_mixer,
                                kStereoMixerParam_Volume,
                                kAudioUnitScope_Input,
                                voice,
                                (float)vol/127,
                                0);
}

void S_SetVoicePanning(u8 voice, u8 panning)
{
    if (voice > ss.num_voices)
        return;
    
    OSStatus err = noErr;
    
    err = AudioUnitSetParameter(ss.au_mixer,
                                kStereoMixerParam_Pan,
                                kAudioUnitScope_Input,
                                voice,
                                (float)panning/255,
                                0);
    
}

void S_SetVoiceFrequency(u8 voice, u32 freq)
{
    if (voice > ss.num_voices)
        return;
        
    OSStatus err = noErr;
    float rate = (float)freq / (float)ss.mixing_rate;
    
    err = AudioUnitSetParameter(ss.voices[voice].au_varispeed,
                                kVarispeedParam_PlaybackRate,
                                kAudioUnitScope_Global,
                                0,
                                rate,
                                0);
}

void S_SetSampleOffset(u8 voice, u32 offset)
{
    if (voice > ss.num_voices)
        return;
    
    ss.voices[voice].sample_pos += offset;
    
    if (ss.voices[voice].sample_pos >= ss.voices[voice].sample_length)
        ss.voices[voice].sample_pos = ss.voices[voice].sample_length;
}

void S_SetSampleLoop(u8 voice, u8 type, u32 start, u32 end)
{
    if (voice > ss.num_voices)
        return;
    
    ss.voices[voice].sample_loop_type = type;
    ss.voices[voice].sample_loop_start = start;
    ss.voices[voice].sample_loop_end = end;
}

