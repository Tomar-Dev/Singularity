// system/process/safe_math.c
#include <stdint.h>

uint64_t safe_div64(uint64_t dividend, uint64_t divisor) {
    if (!divisor) return 0;
    return dividend / divisor;
}

uint64_t safe_mod64(uint64_t dividend, uint64_t divisor) {
    if (!divisor) return 0;
    return dividend % divisor;
}
