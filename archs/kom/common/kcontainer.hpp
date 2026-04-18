// archs/kom/common/kcontainer.hpp
#ifndef KCONTAINER_HPP
#define KCONTAINER_HPP

#ifdef __cplusplus
#include "archs/kom/common/kobject.hpp"
#include "system/sync/rwlock.h"

struct ONSBinding {
    char name[64];
    KObject* obj;
    struct ONSBinding* next;
};

class KContainer : public KObject {
protected:
    struct ONSBinding* head;
    rwlock_t rw_lock;

public:
    KContainer();
    virtual ~KContainer() override;

    virtual error_t bind(const char* name, KObject* obj);
    virtual error_t unbind(const char* name);
    virtual KObject* lookup(const char* name);
    virtual bool enumerate(uint32_t index, char* out_name, KObjectType* out_type);
    
    virtual error_t create_child(const char* name, KObjectType type);
};
#endif // __cplusplus
#endif