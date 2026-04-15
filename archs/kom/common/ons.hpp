// archs/kom/common/ons.hpp
#ifndef ONS_HPP
#define ONS_HPP

#ifdef __cplusplus
#include "archs/kom/common/kcontainer.hpp"
#endif

#define ONS_OK 0
#define ONS_ERR_INVALID_ARG 1
#define ONS_ERR_NOT_FOUND 2
#define ONS_ERR_NOT_CONTAINER 3
#define ONS_ERR_COLLISION 4
#define ONS_ERR_NO_MEMORY 5

#ifdef __cplusplus
extern "C" {
#endif

void ons_init(void);

int ons_bind_c(const char* path, void* obj);

bool ons_enumerate(const char* path, uint32_t index, char* out_name, uint8_t* out_type);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
int ons_bind(const char* path, KObject* obj);
int ons_unbind(const char* path);
KObject* ons_resolve(const char* path);
KObject* ons_resolve_parent(const char* path, char* out_filename);
#endif

#endif // FIX: Eksik olan endif eklendi