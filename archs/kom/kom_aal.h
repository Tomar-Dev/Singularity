// archs/kom/kom_aal.h
#ifndef ARCHS_KOM_AAL_H
#define ARCHS_KOM_AAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
#include "archs/kom/common/kobject.hpp"
#include "archs/kom/common/kblob.hpp"
#include "archs/kom/common/kcontainer.hpp"
#include "archs/kom/common/kdevice.hpp"
#include "archs/kom/common/ons.hpp"
// FIX: Yol common dizinine uyarlandı
#include "archs/kom/common/provider_glue.hpp"
#endif

#include "archs/kom/common/kom.h"
#include "archs/kom/common/kiovec.h"

#ifdef __cplusplus
extern "C" {
#endif

void ons_init(void);
int  ons_bind_c(const char* path, void* obj);
void kobject_unref_c(void* obj);
bool ons_enumerate(const char* path, uint32_t index, char* out_name, uint8_t* out_type);

#ifdef __cplusplus
}
#endif

#endif