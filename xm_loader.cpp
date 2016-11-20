/*
 *  xm_loader.cpp
 *  ca_test
 *
 *  Created by Oliver Markovic on 17.04.08.
 *  Copyright 2008 Oliver Markovic. All rights reserved.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "xm.h"



s32 XM_ReadFileHeader(int fd, XM_module_t* module)
{
    // read id text (must be "Extended Module: ")
    char id[18];
    read(fd, id, 17);
    id[17] = 0;
    if (strcmp(id, "Extended Module: ") != 0)
        return -1;
    
    // module name
    lseek(fd, 20, SEEK_CUR);
    
    // next byte must always be 0x1A
    u8 magic;
    read(fd, &magic, 1);
    if (magic != 0x1A)
        return -1;
    
    // tracker name
    lseek(fd, 20, SEEK_CUR);
    
    // version must be 0x0104
    read(fd, &module->version, 2);
    if (module->version != 0x0104) {
        printf("Warning: Version not 0x0104\n");
        //return -1;
    }
    
    // header size
    lseek(fd, 4, SEEK_CUR);
    
    read(fd, &module->song_length, 2);
    read(fd, &module->song_restart_pos, 2);
    read(fd, &module->num_channels, 2);
    read(fd, &module->num_patterns, 2);
    read(fd, &module->num_instruments, 2);
    read(fd, &module->flags, 2);
    read(fd, &module->default_tempo, 2);
    read(fd, &module->default_bpm, 2);
    read(fd, module->pattern_order, 256);
    
    return 0;
}


void XM_UnpackPattern(int fd, XM_module_t *module, XM_pattern_t *pattern)
{
    // determine packed size of pattern data
    u16 packed_size;
    read(fd, &packed_size, 2);
    
    // empty pattern? initialize
    if (packed_size == 0) {
        memset(pattern->data, 0, pattern->num_rows * module->num_channels * sizeof(XM_note_t));
        return;
    }
    
    // start unpacking
    int k = 0;
    int channel = 0;
    int i = 0;
    while(k < packed_size) {
        XM_note_t *note = &pattern->data[i];
        u8 cur_byte;
        
        k += read(fd, &cur_byte, 1);
        
        if (cur_byte & 0x80) {
            if (cur_byte & 0x01) k += read(fd, &note->note, 1);
            if (cur_byte & 0x02) k += read(fd, &note->instrument, 1);
            if (cur_byte & 0x04) k += read(fd, &note->volume, 1);
            if (cur_byte & 0x08) k += read(fd, &note->fxtype, 1);
            if (cur_byte & 0x10) k += read(fd, &note->fxparam, 1);
        } else {
            note->note = cur_byte;
            k += read(fd, &note->instrument, 1);
            k += read(fd, &note->volume, 1);
            k += read(fd, &note->fxtype, 1);
            k += read(fd, &note->fxparam, 1);
        }
        
        // disambiguate arpeggio (0x0) from no effect (0xFF)
        if (!note->fxtype && !note->fxparam)
            note->fxtype = XM_FX_NO_EFFECT;
        
        i++;
        channel++;
        
        if (channel >= module->num_channels)
            channel = 0;
    }
}

void XM_ReadPattern(int fd, XM_module_t *module, XM_pattern_t *pattern)
{
    // header length & packing_type
    lseek(fd, 5, SEEK_CUR);
    
    read(fd, &pattern->num_rows, 2);
    
    // alloc pattern data
    pattern->data = (XM_note_t*)malloc(pattern->num_rows * module->num_channels * sizeof(XM_note_t));
    memset(pattern->data, 0, pattern->num_rows * module->num_channels * sizeof(XM_note_t));
    
    XM_UnpackPattern(fd, module, pattern);
}


void XM_ReadSampleHeader(int fd, XM_sample_t *sample)
{
    read(fd, &sample->length, 4);
    read(fd, &sample->loop_start, 4);
    read(fd, &sample->loop_length, 4);
    read(fd, &sample->volume, 1);
    read(fd, &sample->finetune, 1);
    read(fd, &sample->type, 1);
    read(fd, &sample->panning, 1);
    read(fd, &sample->relative_note, 1);
    
    // reserved & sample name
    lseek(fd, 1, SEEK_CUR);
    char name[23];
    read(fd, name, 22);
    name[22] = 0;
        
    //lseek(fd, 23, SEEK_CUR);
    /*
     printf(" name: %s\n", name);
     printf(" sample->length: %d\n", sample->length);
     printf(" sample->loop_start: %d\n", sample->loop_start);
     printf(" sample->loop_length: %d\n", sample->loop_length);
     printf(" sample->volume: %d\n", sample->volume);
     printf(" sample->finetune: %d\n", sample->finetune);
     printf(" sample->type: %d\n", sample->type);
     printf(" sample->panning: %d\n", sample->panning);
     printf(" sample->relative_note: %d\n", sample->relative_note);
     */
}

void XM_ReadSampleData(int fd, XM_sample_t *sample)
{
    if (sample->length == 0) {
        sample->data = 0;
        return;
    }
    
    int data_type = sample->type & XM_SAMPLE_16BIT ? 2 : 1;
    
    sample->data = (void*)malloc(sample->length * data_type);
    
    read(fd, sample->data, sample->length * data_type);
    
    // convert sample data from delta-code representation
    if (sample->type & XM_SAMPLE_16BIT) {
        u16 old, new_s;
        u16 *data = (u16*)sample->data;
        
        old = 0;
        for (int i = 0; i < sample->length; i++) {
            new_s = data[i] + old;
            data[i] = new_s;
            old = new_s;
        }
    } else {
        u8 old, new_s;
        u8 *data = (u8*)sample->data;
        
        old = 0;
        for (int i = 0; i < sample->length; i++) {
            new_s = data[i] + old;
            data[i] = new_s;
            old = new_s;
        }
    }
}



void XM_ReadInstrument(int fd, XM_instrument_t *instrument)
{
    u32 header_length;
    read(fd, &header_length, 4);
    
    // name
    //lseek(fd, 22, SEEK_CUR);
    char name[23];
    read(fd, name, 22);
    name[22] = 0;
    
    read(fd, &instrument->type, 1);
    read(fd, &instrument->num_samples, 2);
    
    if (instrument->num_samples > 0) {
        // sample header length
        lseek(fd, 4, SEEK_CUR);
        
        read(fd, instrument->sample_numbers, 96);
        
        for (int i = 0; i < XM_MAX_ENVELOPE_POINTS; i++) {
            read(fd, &instrument->volume_envelope.points[i].frame, 2);
            read(fd, &instrument->volume_envelope.points[i].value, 2);
        }

        for (int i = 0; i < XM_MAX_ENVELOPE_POINTS; i++) {
            read(fd, &instrument->panning_envelope.points[i].frame, 2);
            read(fd, &instrument->panning_envelope.points[i].value, 2);
        }
        
        read(fd, &instrument->volume_envelope.num_points, 1);
        read(fd, &instrument->panning_envelope.num_points, 1);
        read(fd, &instrument->volume_envelope.sustain_point, 1);
        read(fd, &instrument->volume_envelope.loop_start, 1);
        read(fd, &instrument->volume_envelope.loop_end, 1);
        read(fd, &instrument->panning_envelope.sustain_point, 1);
        read(fd, &instrument->panning_envelope.loop_start, 1);
        read(fd, &instrument->panning_envelope.loop_end, 1);
        read(fd, &instrument->volume_envelope.flags, 1);
        read(fd, &instrument->panning_envelope.flags, 1);
        read(fd, &instrument->vibrato_type, 1);
        read(fd, &instrument->vibrato_sweep, 1);
        read(fd, &instrument->vibrato_depth, 1);
        read(fd, &instrument->vibrato_rate, 1);
        read(fd, &instrument->volume_fadeout, 2);
        
        // skip reserved
        lseek(fd, 2, SEEK_CUR);
        
        lseek(fd, header_length - 243, SEEK_CUR);
    } else
        lseek(fd, header_length - 29, SEEK_CUR);
    
    // prepare to read samples
    instrument->samples = (XM_sample_t*)malloc(instrument->num_samples * sizeof(XM_sample_t));
    
    // for some strange reason, the header and data is not stored continously
    for (int i = 0; i < instrument->num_samples; i++)
        XM_ReadSampleHeader(fd, &instrument->samples[i]);
}


s32 XM_LoadFile(const char *file, XM_module_t *module)
{
    int fd = open(file, O_RDONLY, 0);
    
    if (fd == -1) {
        perror("XM_LoadFile");
        return -1;
    }
    
    // read header
    if (XM_ReadFileHeader(fd, module) < 0)
        return -1;
    
    // alloc data
    module->patterns = (XM_pattern_t*)malloc(module->num_patterns * sizeof(XM_pattern_t));
    module->instruments = (XM_instrument_t*)malloc(module->num_instruments * sizeof(XM_instrument_t));

    // read instruments, samples and pattern data
    // XM has a different layout for differing versions
    if (module->version >= 0x0104) {
        for (int i = 0; i < module->num_patterns; i++)
            XM_ReadPattern(fd, module, &module->patterns[i]);
    
        for (int i = 0; i < module->num_instruments; i++) {
            XM_ReadInstrument(fd, &module->instruments[i]);
        
            for (int s = 0; s < module->instruments[i].num_samples; s++)
                XM_ReadSampleData(fd, &module->instruments[i].samples[s]);
        }
    } else {
        for (int i = 0; i < module->num_instruments; i++)
            XM_ReadInstrument(fd, &module->instruments[i]);

        for (int i = 0; i < module->num_patterns; i++)
            XM_ReadPattern(fd, module, &module->patterns[i]);

        for (int i = 0; i < module->num_instruments; i++)
            for (int s = 0; s < module->instruments[i].num_samples; s++)
                XM_ReadSampleData(fd, &module->instruments[i].samples[s]);
    }
    
    close(fd);
    
    return 0;
}




