// system/init/tar_inflater.cpp
#include "system/init/tar_inflater.hpp"
#include "libc/string.h"
#include "libc/stdio.h"
#include "memory/kheap.h"
#include "archs/kom/kom_aal.h"

extern "C" void print_status(const char* prefix, const char* msg, const char* status);

struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
};

static uint32_t oct2bin(const char* str, int size) {
    uint32_t n = 0;
    const char* c = str;
    while (size-- > 0) {
        if (*c < '0' || *c > '7') { break; }
        n *= 8;
        n += *c - '0';
        c++;
    }
    return n;
}

static bool is_pax_entry(const TarHeader* hdr) {
    if (hdr->typeflag == 'x' || hdr->typeflag == 'X' || hdr->typeflag == 'g') {
        return true;
    } else if (strncmp(hdr->name, "@PaxHeader", 10) == 0) {
        return true;
    } else if (strncmp(hdr->name, "PaxHeaders/", 11) == 0) {
        return true;
    } else {
        return false;
    }
}

extern "C" void tar_inflater_init(void* addr, uint32_t size) {
    uint64_t current_addr = (uint64_t)addr;
    uint64_t end_addr = current_addr + size;

    KObject* root_obj = ons_resolve("/");
    if (!root_obj || root_obj->type != KObjectType::CONTAINER) {
        if (root_obj) { kobject_unref(root_obj); }
        return;
    }
    KContainer* root = (KContainer*)root_obj;

    while (current_addr < end_addr) {
        TarHeader* header = (TarHeader*)current_addr;

        if (header->name[0] == '\0') { break; }

        uint32_t fileSize = oct2bin(header->size, 11);

        if (is_pax_entry(header)) {
            uint32_t skip = 512 + fileSize;
            if (skip % 512 != 0) { skip += 512 - (skip % 512); }
            current_addr += skip;
            continue;
        }

        bool is_dir = (header->typeflag == '5' ||
                       (fileSize == 0 && header->name[strlen(header->name) - 1] == '/'));

        char pathBuffer[101];
        memset(pathBuffer, 0, 101);
        strncpy(pathBuffer, header->name, 100);

        KContainer* currentParent = root;
        kobject_ref(currentParent);

        char* p = pathBuffer;
        char* token_start = p;

        while (true) {
            if (*p == '/' || *p == '\0') {
                char saved_char = *p;
                *p = '\0';

                if (strlen(token_start) > 0 && strcmp(token_start, ".") != 0) {
                    bool is_last_token = (saved_char == '\0');
                    KObject* found = currentParent->lookup(token_start);

                    if (is_last_token) {
                        if (!found) {
                            if (is_dir) {
                                KContainer* newDir = new KContainer();
                                currentParent->bind(token_start, newDir);
                                kobject_unref(newDir);
                            } else {
                                KVolatileBlob* newFile = new KVolatileBlob(
                                    token_start,
                                    (void*)(current_addr + 512),
                                    fileSize);
                                currentParent->bind(token_start, newFile);
                                kobject_unref(newFile);
                            }
                        } else {
                            kobject_unref(found);
                        }
                    } else {
                        if (!found) {
                            KContainer* newDir = new KContainer();
                            currentParent->bind(token_start, newDir);
                            
                            kobject_unref(currentParent);
                            // FIX: Bellek Sızıntısı (Memory Leak) Engellendi.
                            // bind() zaten nesneye referans ekler. currentParent yeni oluşturulan
                            // nesneyi işaret etmeden önce ekstra bir kobject_ref() çağrılmasından 
                            // kaçınılarak sahte (hayalet) referans bırakılması engellendi.
                            currentParent = newDir; 
                        } else {
                            kobject_unref(currentParent);
                            currentParent = (KContainer*)found;
                        }
                    }
                }

                if (saved_char == '\0') { break; }
                token_start = p + 1;
            }
            p++;
        }

        kobject_unref(currentParent);

        uint32_t offset = 512 + fileSize;
        if (offset % 512 != 0) { offset += 512 - (offset % 512); }
        current_addr += offset;
    }

    kobject_unref(root);
    print_status("[ KOM  ]", "Boot Archive (TAR) Inflated as Native Objects", "OK");
}