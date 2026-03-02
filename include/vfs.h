#pragma once
#include <stdint.h>
#include <stddef.h>

// Simple VFS (flat table, sector-backed files)
static const int VFS_MAX_ENTRIES  = 128;
static const int VFS_MAX_PATH     = 256;
static const int VFS_MAX_FILESIZE = 8192;

enum VFSType { VFS_FILE = 0, VFS_DIR = 1 };

struct VFSEntry {
    char     path[VFS_MAX_PATH]; // full path
    uint32_t start_lba;          // starting disk sector
    uint32_t sector_count;       // sectors used
    uint32_t size;               // file size in bytes
    bool     used;               // entry active?
    VFSType  type;               // file or directory
};



// Init 
void vfs_init();

//  Path helpers 
void vfs_resolve(const char* rel, char* out, int out_len); // rel → absolute
void vfs_getcwd(char* buf, int len);
int  vfs_chdir(const char* path);   // 0 = ok, -1 = not found

//  File ops 
int      vfs_exists(const char* path);   // index or -1
int      vfs_create(const char* path);
int      vfs_write(const char* path, const char* data, uint32_t len);
int      vfs_append(const char* path, const char* data, uint32_t len);
int      vfs_read(const char* path, char* buf, uint32_t buf_len);
int      vfs_delete(const char* path);
uint32_t vfs_size(const char* path);

//  Directory ops 
int  vfs_mkdir(const char* path);
int  vfs_rmdir(const char* path);           // removes dir and all children

//  Move / copy 
int  vfs_mv(const char* src, const char* dst);
int  vfs_cp(const char* src, const char* dst);

//  Listing 
void vfs_listdir(const char* dir,
                 char names[][VFS_MAX_PATH],
                 bool  is_dir[],
                 int*  count);

//  Disk info
uint32_t vfs_get_disk_size_mb();

uint32_t vfs_get_used_sectors();