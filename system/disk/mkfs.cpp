// system/disk/mkfs.cpp
#include "system/device/device.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "memory/kheap.h"
#include "system/disk/cache.hpp"

#define EXT2_SIGNATURE 0xEF53

struct ext_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_padding1;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_reserved_char_pad;
    uint16_t s_reserved_word_pad;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint8_t  s_reserved[760];
} __attribute__((packed));

struct ext_bg_descriptor {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed));

#define EXT2_FEATURE_COMPAT_DIR_PREALLOC    0x0001
#define EXT2_FEATURE_COMPAT_HAS_JOURNAL     0x0004 
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE   0x0002
#define EXT2_FEATURE_INCOMPAT_FILETYPE      0x0002
#define EXT4_FEATURE_INCOMPAT_EXTENTS       0x0040 

struct ext4_extent_header {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} __attribute__((packed));

struct ext4_extent {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} __attribute__((packed));

struct ext_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];   
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed));

struct ext_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed));

#define EXT4_EXTENTS_FLAG 0x80000

struct fat32_bs {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fats_count;
    uint16_t dir_entries_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t heads_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed));

struct fat32_fsinfo {
    uint32_t lead_sig;       
    uint8_t  reserved1[480];
    uint32_t struct_sig;     
    uint32_t free_count;     
    uint32_t next_free;      
    uint8_t  reserved2[12];
    uint32_t trail_sig;      
} __attribute__((packed));

extern "C" void mkfs_fat32_impl(Device* dev, const char* label) {
    dev->unlockWrite(DEVICE_MAGIC_UNLOCK);

    printf("[MKFS] Preparing FAT32 volume...\n");
    uint8_t* zero_buf = (uint8_t*)kmalloc(512);
    if (!zero_buf) { printf("[MKFS] OOM during format.\n"); dev->lockWrite(); return; }
    memset(zero_buf, 0, 512);
    
    for (int i = 0; i < 64; i++) {
        if (!dev->writeBlock(i, 1, zero_buf)) {
            printf("[MKFS] Format aborted due to write error. (Lockdown active?)\n");
            kfree(zero_buf);
            dev->lockWrite();
            return;
        }
    }
    
    uint64_t total_sectors = dev->getCapacity() / 512;
    uint32_t sectors_per_cluster = 8; 
    if (total_sectors > 1024*1024*4) sectors_per_cluster = 32; 
    
    uint32_t reserved_sectors = 32;
    uint32_t number_of_fats = 2;
    
    uint64_t data_sectors = total_sectors - reserved_sectors;
    uint32_t fat_sectors = (uint32_t)((data_sectors / sectors_per_cluster) * 4 / 512) + 1;

    struct fat32_bs* bs = (struct fat32_bs*)zero_buf;
    bs->jump[0] = 0xEB; bs->jump[1] = 0x58; bs->jump[2] = 0x90;
    memcpy(bs->oem, "SINGULAR", 8);
    bs->bytes_per_sector = 512;
    bs->sectors_per_cluster = (uint8_t)sectors_per_cluster;
    bs->reserved_sectors = (uint16_t)reserved_sectors;
    bs->fats_count = (uint8_t)number_of_fats;
    bs->total_sectors_32 = (uint32_t)total_sectors;
    bs->sectors_per_fat_32 = fat_sectors;
    bs->root_cluster = 2;
    bs->fs_info = 1;
    bs->backup_boot_sector = 6;
    bs->boot_signature = 0x29;
    bs->volume_id = 0x12345678; 
    bs->hidden_sectors = (uint32_t)dev->getStartLBA(); 
    
    memset(bs->volume_label, ' ', 11);
    int label_len = strlen(label);
    if (label_len > 11) label_len = 11;
    memcpy(bs->volume_label, label, label_len);
    memcpy(bs->fs_type, "FAT32   ", 8);
    
    zero_buf[510] = 0x55;
    zero_buf[511] = 0xAA;
    
    dev->writeBlock(0, 1, zero_buf);
    dev->writeBlock(6, 1, zero_buf); 
    
    memset(zero_buf, 0, 512);
    struct fat32_fsinfo* fsinfo = (struct fat32_fsinfo*)zero_buf;
    fsinfo->lead_sig = 0x41615252;
    fsinfo->struct_sig = 0x61417272;
    fsinfo->free_count = (data_sectors / sectors_per_cluster) - 1;
    fsinfo->next_free = 3;
    fsinfo->trail_sig = 0xAA550000;
    
    dev->writeBlock(1, 1, zero_buf);
    dev->writeBlock(7, 1, zero_buf);
    
    memset(zero_buf, 0, 512);
    uint32_t* fat = (uint32_t*)zero_buf;
    fat[0] = 0x0FFFFFF8; 
    fat[1] = 0xFFFFFFFF; 
    fat[2] = 0x0FFFFFFF; 
    
    uint64_t fat1_start = reserved_sectors;
    dev->writeBlock(fat1_start, 1, zero_buf);
    uint64_t fat2_start = fat1_start + fat_sectors;
    dev->writeBlock(fat2_start, 1, zero_buf);
    
    memset(zero_buf, 0, 512);
    uint64_t root_dir_lba = reserved_sectors + (number_of_fats * fat_sectors);
    
    for(uint32_t i=0; i<sectors_per_cluster; i++) {
        dev->writeBlock(root_dir_lba + i, 1, zero_buf);
    }

    DiskCache::invalidateDevice(dev);
    
    printf("[MKFS] Format Complete. New FS: FAT32 (Hidden: %u)\n", bs->hidden_sectors);
    kfree(zero_buf); 

    dev->lockWrite();
}

extern "C" void mkfs_ext4_impl(Device* dev, const char* label) { 
    dev->unlockWrite(DEVICE_MAGIC_UNLOCK);

    uint64_t total_sectors = dev->getCapacity() / 512;
    uint32_t block_size = 1024; 
    uint32_t total_blocks = (uint32_t)(total_sectors / 2);
    
    uint32_t blocks_per_group = 8192;
    uint32_t group_count = (total_blocks + blocks_per_group - 1) / blocks_per_group;
    if (group_count == 0) group_count = 1; 
    if (group_count > 1024) group_count = 1024; 
    
    uint32_t inode_size = 256; 
    uint32_t inodes_per_group = 2048;
    uint32_t inode_table_blocks = (inodes_per_group * inode_size) / block_size;

    printf("[MKFS] Ext4 Geometry: %u blocks, %u groups (Inode Size: %d)\n", total_blocks, group_count, inode_size);

    uint8_t* buf = (uint8_t*)kmalloc(block_size);
    if (!buf) {
        printf("[MKFS] OOM: Failed to allocate superblock buffer.\n");
        dev->lockWrite();
        return;
    }
    memset(buf, 0, block_size);

    struct ext_superblock* sb = (struct ext_superblock*)buf;
    sb->s_inodes_count = group_count * inodes_per_group;
    sb->s_blocks_count = total_blocks;
    sb->s_free_blocks_count = total_blocks - (group_count * (inode_table_blocks + 5));
    sb->s_free_inodes_count = sb->s_inodes_count - 11; 
    sb->s_first_data_block = 1; 
    sb->s_log_block_size = 0;   
    sb->s_blocks_per_group = blocks_per_group;
    sb->s_inodes_per_group = inodes_per_group;
    sb->s_magic = EXT2_SIGNATURE;
    sb->s_rev_level = 1; 
    sb->s_first_ino = 11;
    sb->s_inode_size = (uint16_t)inode_size;
    strncpy(sb->s_volume_name, label, 16);
    
    sb->s_feature_compat = EXT2_FEATURE_COMPAT_DIR_PREALLOC | EXT2_FEATURE_COMPAT_HAS_JOURNAL;
    sb->s_feature_ro_compat = EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER | EXT2_FEATURE_RO_COMPAT_LARGE_FILE;
    sb->s_feature_incompat = EXT2_FEATURE_INCOMPAT_FILETYPE | EXT4_FEATURE_INCOMPAT_EXTENTS;
    
    if (!dev->writeBlock(2, 2, buf)) {
        printf("[MKFS] Format aborted due to write error. (Lockdown active?)\n");
        kfree(buf);
        dev->lockWrite();
        return;
    }
    
    uint32_t bgdt_size_bytes = group_count * sizeof(struct ext_bg_descriptor);
    uint32_t bgdt_blocks = (bgdt_size_bytes + block_size - 1) / block_size;
    
    uint8_t* bgdt_buf = (uint8_t*)kmalloc(bgdt_blocks * block_size);
    if (!bgdt_buf) {
        printf("[MKFS] OOM: Failed to allocate BGDT buffer.\n");
        kfree(buf);
        dev->lockWrite();
        return;
    }
    memset(bgdt_buf, 0, bgdt_blocks * block_size);
    struct ext_bg_descriptor* bg_table = (struct ext_bg_descriptor*)bgdt_buf;

    for (uint32_t g = 0; g < group_count; g++) {
        uint32_t group_start_block = sb->s_first_data_block + (g * blocks_per_group);
        uint32_t metadata_offset = (g == 0) ? (1 + bgdt_blocks) : 0;
        
        bg_table[g].bg_block_bitmap = group_start_block + metadata_offset + 0;
        bg_table[g].bg_inode_bitmap = group_start_block + metadata_offset + 1;
        bg_table[g].bg_inode_table  = group_start_block + metadata_offset + 2;
        bg_table[g].bg_free_blocks_count = blocks_per_group - (metadata_offset + 2 + inode_table_blocks);
        bg_table[g].bg_free_inodes_count = inodes_per_group;
        bg_table[g].bg_used_dirs_count = 0;
        
        if (g == 0) {
            bg_table[g].bg_free_inodes_count -= 11; 
            bg_table[g].bg_used_dirs_count = 1;     
        }

        memset(buf, 0, block_size);
        uint32_t used_blocks = metadata_offset + 2 + inode_table_blocks;
        for(uint32_t k=0; k<used_blocks; k++) buf[k/8] |= (1 << (k%8));
        dev->writeBlock(bg_table[g].bg_block_bitmap * 2, 2, buf);
        
        memset(buf, 0, block_size);
        if (g == 0) {
            buf[0] = 0xFF; buf[1] = 0x03; 
        }
        dev->writeBlock(bg_table[g].bg_inode_bitmap * 2, 2, buf);
        
        memset(buf, 0, block_size);
        for(uint32_t k=0; k<inode_table_blocks; k++) {
            dev->writeBlock((bg_table[g].bg_inode_table + k) * 2, 2, buf);
        }
    }

    dev->writeBlock(4, bgdt_blocks * 2, bgdt_buf);
    kfree(bgdt_buf);

    memset(buf, 0, block_size);
    struct ext_inode* root_inode = (struct ext_inode*)(buf + inode_size); 
    root_inode->i_mode = 0x41ED; 
    root_inode->i_size = 1024;
    root_inode->i_links_count = 2;
    root_inode->i_blocks = 2; 
    root_inode->i_flags = EXT4_EXTENTS_FLAG; 
    uint32_t root_data_block = bg_table[0].bg_inode_table + inode_table_blocks;
    
    struct ext4_extent_header* eh = (struct ext4_extent_header*)root_inode->i_block;
    eh->eh_magic = 0xF30A;
    eh->eh_entries = 1;
    eh->eh_max = (sizeof(root_inode->i_block) - sizeof(struct ext4_extent_header)) / sizeof(struct ext4_extent);
    eh->eh_depth = 0;
    eh->eh_generation = 0;

    struct ext4_extent* ext = (struct ext4_extent*)(eh + 1);
    ext->ee_block = 0;
    ext->ee_len = 1;
    ext->ee_start_lo = root_data_block;
    ext->ee_start_hi = 0;

    dev->writeBlock(bg_table[0].bg_inode_table * 2, 2, buf);
    
    memset(buf, 0, block_size);
    struct ext_dir_entry* de = (struct ext_dir_entry*)buf;
    de->inode = 2; de->rec_len = 12; de->name_len = 1; de->file_type = 2; de->name[0] = '.';
    de = (struct ext_dir_entry*)((uint8_t*)de + 12);
    de->inode = 2; de->rec_len = 1012; de->name_len = 2; de->file_type = 2; de->name[0] = '.'; de->name[1] = '.';
    dev->writeBlock(root_data_block * 2, 2, buf);
    
    DiskCache::invalidateDevice(dev);
    kfree(buf);
    printf("[MKFS] Format Complete. New FS: Ext4 (Journal-less, Extents Enabled)\n");

    dev->lockWrite();
}

extern "C" {
    void mkfs_fat32(const char* dev_name, const char* label) {
        Device* dev = DeviceManager::getDevice(dev_name);
        if (!dev) { printf("[MKFS] Device not found.\n"); return; }
        if (dev->getType() != DEV_PARTITION) { printf("[MKFS] Error: Partition required.\n"); return; }
        mkfs_fat32_impl(dev, label);
        device_release(dev);
    }
    
    void mkfs_ext4(const char* dev_name, const char* label) {
        Device* dev = DeviceManager::getDevice(dev_name);
        if (!dev) { printf("[MKFS] Device not found.\n"); return; }
        if (dev->getType() != DEV_PARTITION) { printf("[MKFS] Error: Partition required.\n"); return; }
        mkfs_ext4_impl(dev, label);
        device_release(dev);
    }
}