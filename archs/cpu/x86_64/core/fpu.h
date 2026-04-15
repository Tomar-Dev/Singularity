// archs/cpu/x86_64/core/fpu.h
#ifndef FPU_H
#define FPU_H

// If floating-point operations (float/double) or optimized memory functions are used without calling this function, the system crashes (#UD).
void init_fpu();

#endif
