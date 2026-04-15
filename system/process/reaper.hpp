// system/process/reaper.hpp
#ifndef REAPER_HPP
#define REAPER_HPP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_reaper();
void signal_reaper();

#ifdef __cplusplus
}

class GrimReaper {
public:
    static void init();
    static void run();
    
private:
    static bool reapOne();
};

#endif

#endif
