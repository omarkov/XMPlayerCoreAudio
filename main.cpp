#include <sys/time.h>
#include <stdio.h>
#include "audio.h"
#include "xm.h"

int get_milliseconds()
{
    struct timeval tv;
    static long start;
    
    gettimeofday (&tv, 0);
    
    if (!start) {
        start = tv.tv_sec;
        return tv.tv_usec / 1000;
    }
    
    return (tv.tv_sec - start) * 1000 + tv.tv_usec / 1000;
}

int main (int argc, char **argv)
{
    // load the module
    XM_module_t module;
    if (XM_LoadFile(argv[1], &module) < 0) {
        printf("Unable to load module.\n");
        return 3;
    }
    
    XM_InitPlayer(&module);

    int tick_duration = 1000 / (2 * module.default_bpm / 5);
    
    S_Init(module.num_channels, 44100);
    
    int t0, t1;
    int frameTime;
    int done = 0;
        
    t0 = get_milliseconds();
        
    while (!done) {
        t1 = get_milliseconds();
        frameTime = 0;
            
        // run a logic frame
        while ((t1 - t0) > tick_duration) {
            XM_RunTick();
                
            tick_duration = 1000 / (2 * XM_GetCurrentBPM() / 5);
                
            t0 += tick_duration;
            frameTime += tick_duration;
            
            t1 = get_milliseconds();
        }
            
        // discard pending time
        if ((t1 - t0) > tick_duration)
            t0 = t1 - tick_duration;
    }
    
    S_Shutdown();
    
    return 0;
}
