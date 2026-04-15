// archs/kom/common/ons.cpp
#include "archs/kom/common/ons.hpp"
#include "libc/string.h"
#include "libc/stdio.h"
#include "memory/kheap.h"

inline void* operator new(size_t, void* p) { return p; }

static KContainer* ons_root = nullptr;
// Defansif: Hizalama zırhı eklendi.
alignas(KContainer) static uint8_t ons_root_buf[sizeof(KContainer)];

extern "C" {

void ons_init() {
    ons_root = new (ons_root_buf) KContainer();
    
    KContainer* c_devices = new KContainer();
    KContainer* c_volumes = new KContainer();
    KContainer* c_system  = new KContainer();
    KContainer* c_ramdisk = new KContainer();
    KContainer* c_ipc     = new KContainer();

    ons_root->bind("devices", c_devices);
    ons_root->bind("volumes", c_volumes);
    ons_root->bind("system", c_system);
    ons_root->bind("ramdisk", c_ramdisk);
    ons_root->bind("ipc", c_ipc);

    kobject_unref(c_devices);
    kobject_unref(c_volumes);
    kobject_unref(c_system);
    kobject_unref(c_ramdisk);
    kobject_unref(c_ipc);

    printf("[ ONS  ] Object Namespace Tree Initialized (NT Path Resolver Active)\n");
}

int ons_bind_c(const char* path, void* obj) {
    return ons_bind(path, static_cast<KObject*>(obj));
}

bool ons_enumerate(const char* path, uint32_t index, char* out_name, uint8_t* out_type) {
    KObject* target = ons_resolve(path);
    if (!target) return false;
    
    if (target->type != KObjectType::CONTAINER) {
        kobject_unref(target);
        return false;
    }
    
    KObjectType t;
    bool success = static_cast<KContainer*>(target)->enumerate(index, out_name, &t);
    if (success && out_type) *out_type = static_cast<uint8_t>(t);
    
    kobject_unref(target);
    return success;
}

}

static void convert_nt_path(const char* in, char* out) {
    if (((in[0] >= 'A' && in[0] <= 'Z') || (in[0] >= 'a' && in[0] <= 'z')) && in[1] == ':') {
        char drive = in[0];
        if (drive >= 'a' && drive <= 'z') drive -= 32; 
        
        if (drive == 'C') {
            snprintf(out, 255, "/");
            if (in[2] == '\\' || in[2] == '/') {
                if (in[3] != '\0') strncat(out, &in[3], 255 - strlen(out));
            } else if (in[2] != '\0') {
                strncat(out, &in[2], 255 - strlen(out));
            }
        } else {
            snprintf(out, 255, "/volumes/%c/", drive);
            if (in[2] == '\\' || in[2] == '/') {
                if (in[3] != '\0') strncat(out, &in[3], 255 - strlen(out));
            } else if (in[2] != '\0') {
                strncat(out, &in[2], 255 - strlen(out));
            }
        }
    } else {
        strncpy(out, in, 255);
    }
    
    for(int i=0; out[i]; i++) if(out[i] == '\\') out[i] = '/';
    
    char temp[256];
    int j = 0;
    for(int i=0; out[i]; i++) {
        if (out[i] == '/' && out[i+1] == '/') continue;
        temp[j++] = out[i];
    }
    temp[j] = '\0';
    
    if (temp[0] == '\0') {
        temp[0] = '/';
        temp[1] = '\0';
    }
    
    strncpy(out, temp, 255);
    out[255] = '\0';
}

KObject* ons_resolve(const char* path) {
    if (!path) return nullptr;
    
    char path_buf[256];
    memset(path_buf, 0, 256);
    convert_nt_path(path, path_buf);

    if (path_buf[0] != '/') return nullptr;
    if (path_buf[1] == '\0') {
        kobject_ref(ons_root);
        return ons_root;
    }
    
    KContainer* current = ons_root;
    kobject_ref(current);
    
    char* token = path_buf + 1;
    char* next_slash = nullptr;
    
    while (token && *token) {
        next_slash = strchr(token, '/');
        if (next_slash) {
            *next_slash = '\0';
        }
        
        if (strlen(token) == 0) {
            if (next_slash) token = next_slash + 1;
            else break;
            continue;
        }
        
        KObject* next_obj = current->lookup(token);
        kobject_unref(current); 
        
        if (!next_obj) return nullptr;
        
        if (next_slash) {
            if (next_obj->type != KObjectType::CONTAINER) {
                kobject_unref(next_obj);
                return nullptr; 
            }
            current = static_cast<KContainer*>(next_obj);
            token = next_slash + 1;
        } else {
            return next_obj;
        }
    }
    
    return current;
}

KObject* ons_resolve_parent(const char* path, char* out_filename) {
    if (!path || !out_filename) return nullptr;
    
    char parentPath[256];
    memset(parentPath, 0, 256);
    convert_nt_path(path, parentPath);
    
    int len = static_cast<int>(strlen(parentPath));
    while (len > 1 && parentPath[len-1] == '/') {
        parentPath[len-1] = '\0';
        len--;
    }
    
    char* lastSlash = strrchr(parentPath, '/');
    if (lastSlash) {
        // FIX: strcpy -> strncpy (Clang-Tidy Security Fix)
        strncpy(out_filename, lastSlash + 1, 127);
        out_filename[127] = '\0';
        
        if (lastSlash == parentPath) *(lastSlash + 1) = '\0';
        else *lastSlash = '\0';
        return ons_resolve(parentPath);
    } else {
        strncpy(out_filename, path, 127);
        out_filename[127] = '\0';
        return ons_resolve("/");
    }
}

int ons_bind(const char* path, KObject* obj) {
    if (!path || !obj) return ONS_ERR_INVALID_ARG;
    
    char path_buf[256];
    memset(path_buf, 0, 256);
    convert_nt_path(path, path_buf);

    if (path_buf[0] != '/') return ONS_ERR_INVALID_ARG;
    
    char* last_slash = strrchr(path_buf, '/');
    if (!last_slash) return ONS_ERR_INVALID_ARG;
    
    const char* name = last_slash + 1;
    if (strlen(name) == 0) return ONS_ERR_INVALID_ARG; 
    
    KContainer* parent = ons_root;
    
    if (last_slash != path_buf) {
        *last_slash = '\0'; 
        KObject* parent_obj = ons_resolve(path_buf);
        
        if (!parent_obj) return ONS_ERR_NOT_FOUND;
        if (parent_obj->type != KObjectType::CONTAINER) {
            kobject_unref(parent_obj);
            return ONS_ERR_NOT_CONTAINER; 
        }
        parent = static_cast<KContainer*>(parent_obj);
    } else {
        kobject_ref(ons_root);
    }
    
    int res = parent->bind(name, obj);
    kobject_unref(parent); 
    
    return res == 0 ? ONS_OK : ONS_ERR_COLLISION; 
}

int ons_unbind(const char* path) {
    if (!path) return ONS_ERR_INVALID_ARG;

    char path_buf[256];
    memset(path_buf, 0, 256);
    convert_nt_path(path, path_buf);

    if (path_buf[0] != '/') return ONS_ERR_INVALID_ARG;
    
    char* last_slash = strrchr(path_buf, '/');
    if (!last_slash) return ONS_ERR_INVALID_ARG;
    
    const char* name = last_slash + 1;
    if (strlen(name) == 0) return ONS_ERR_INVALID_ARG;
    
    KContainer* parent = ons_root;
    
    if (last_slash != path_buf) {
        *last_slash = '\0'; 
        KObject* parent_obj = ons_resolve(path_buf);
        
        if (!parent_obj) return ONS_ERR_NOT_FOUND;
        if (parent_obj->type != KObjectType::CONTAINER) {
            kobject_unref(parent_obj);
            return ONS_ERR_NOT_CONTAINER; 
        }
        parent = static_cast<KContainer*>(parent_obj);
    } else {
        kobject_ref(ons_root);
    }
    
    int res = parent->unbind(name);
    kobject_unref(parent);
    
    return res == 0 ? ONS_OK : ONS_ERR_NOT_FOUND;
}