#include "../include/vfs.h"
#include "../include/kstring.h"
#include "../include/ata.h"

static VFSEntry entries[VFS_MAX_ENTRIES];
static char     vfs_cwd[VFS_MAX_PATH] = "/";

//  PERSISTENT STORAGE VARIABLES & HELPERS 
#define MAX_SECTORS 8388608 // 4GB = 8M sectors
static uint32_t disk_bitmap[MAX_SECTORS / 32];

static void b_set(uint32_t lba) { disk_bitmap[lba / 32] |= (1u << (lba % 32)); }
static void b_clr(uint32_t lba) { disk_bitmap[lba / 32] &= ~(1u << (lba % 32)); }
static bool b_tst(uint32_t lba) { return disk_bitmap[lba / 32] & (1u << (lba % 32)); }

static int find_free_sectors(uint32_t count, uint32_t* out_lba) {
    if (count == 0) return 0;
    uint32_t consecutive = 0; uint32_t start = 0;
    for (uint32_t i = 128; i < MAX_SECTORS; i++) { // Reserve LBA 0-127 for MFT
        if (!b_tst(i)) {
            if (consecutive == 0) start = i;
            if (++consecutive == count) { *out_lba = start; return 0; }
        } else { consecutive = 0; }
    }
    return -1;
}

static void save_mft() {
    uint8_t buffer[512];
    uint8_t* mft_ptr = (uint8_t*)entries;
    uint32_t mft_size = sizeof(entries);
    uint32_t sectors = (mft_size + 511) / 512;
    for (uint32_t i = 0; i < sectors; i++) {
        k_memset(buffer, 0, 512);
        uint32_t to_copy = mft_size > 512 ? 512 : mft_size;
        k_memcpy(buffer, mft_ptr, to_copy);
        ata_write_sector(1 + i, buffer);
        mft_ptr += 512; mft_size -= to_copy;
    }
}

//  Internal path helpers 

// Join absolute base + relative name → out
static void path_join(const char* base, const char* name, char* out, int len) {
    if (name[0] == '/') { k_strncpy(out, name, len-1); out[len-1]='\0'; return; }
    int bl = k_strlen(base);
    k_strncpy(out, base, len-1); out[len-1]='\0';
    if (base[bl-1] != '/' && bl < len-2) { out[bl]='/'; bl++; out[bl]='\0'; }
    k_strncpy(out+bl, name, len-bl-1); out[len-1]='\0';
}

// Get parent directory
static void path_parent(const char* path, char* out, int len) {
    k_strncpy(out, path, len-1); out[len-1]='\0';
    int l = k_strlen(out);
    for (int i = l-1; i > 0; i--) { if (out[i]=='/') { out[i]='\0'; return; } }
    out[0]='/'; out[1]='\0';
}

// If path is a DIRECT child of dir, return pointer to its name part, else nullptr
static const char* direct_child(const char* path, const char* dir) {
    int dl = k_strlen(dir);
    if (k_strncmp(path, dir, dl) != 0) return nullptr;
    const char* rest;
    if (dir[dl-1] == '/') rest = path + dl;
    else { if (path[dl] != '/') return nullptr; rest = path + dl + 1; }
    if (!*rest) return nullptr;
    for (const char* p = rest; *p; p++) if (*p == '/') return nullptr;
    return rest;
}


//  Public path API 
void vfs_resolve(const char* rel, char* out, int len) {
    if (!rel || !rel[0]) { k_strncpy(out, vfs_cwd, len-1); out[len-1]='\0'; return; }
    if (k_strcmp(rel, "..") == 0) { path_parent(vfs_cwd, out, len); return; }
    if (k_strcmp(rel, ".")  == 0) { k_strncpy(out, vfs_cwd, len-1); out[len-1]='\0'; return; }
    if (rel[0] == '/')            { k_strncpy(out, rel,     len-1); out[len-1]='\0'; return; }
    path_join(vfs_cwd, rel, out, len);
}

void vfs_getcwd(char* buf, int len) { k_strncpy(buf, vfs_cwd, len-1); buf[len-1]='\0'; }


//  Init 
void vfs_init() {
    k_memset(disk_bitmap, 0, sizeof(disk_bitmap));
    for (int i = 0; i < 128; i++) b_set(i); // Reserve LBA 0-127 for OS/MFT

    // Load MFT from disk
    uint8_t buffer[512];
    uint8_t* mft_ptr = (uint8_t*)entries;
    uint32_t mft_size = sizeof(entries);
    uint32_t sectors = (mft_size + 511) / 512;

    for (uint32_t i = 0; i < sectors; i++) {
        ata_read_sector(1 + i, buffer);
        uint32_t to_copy = mft_size > 512 ? 512 : mft_size;
        k_memcpy(mft_ptr, buffer, to_copy);
        mft_ptr += 512; mft_size -= to_copy;
    }

    // Rebuild Free Space Bitmap
    for (int i = 0; i < VFS_MAX_ENTRIES; i++) {
        if (entries[i].used && entries[i].type == VFS_FILE) {
            for (uint32_t s = 0; s < entries[i].sector_count; s++) {
                b_set(entries[i].start_lba + s);
            }
        }
    }
    k_strcpy(vfs_cwd, "/");
}


//  Internal helpers 
static int find_abs(const char* p) {
    for (int i = 0; i < VFS_MAX_ENTRIES; i++)
        if (entries[i].used && k_strcmp(entries[i].path, p) == 0) return i;
    return -1;
}
static int free_slot() {
    for (int i = 0; i < VFS_MAX_ENTRIES; i++) if (!entries[i].used) return i;
    return -1;
}


//  File operations 
int vfs_exists(const char* path) {
    char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
    return find_abs(abs);
}

int vfs_create(const char* path) {
    char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
    if (find_abs(abs) >= 0) return -1;
    int s = free_slot(); if (s < 0) return -1;
    k_strncpy(entries[s].path, abs, VFS_MAX_PATH-1);
    entries[s].type = VFS_FILE; 
    entries[s].size = 0;
    entries[s].start_lba = 0; 
    entries[s].sector_count = 0;
    entries[s].used = true;
    save_mft();
    return s;
}

int vfs_write(const char* path, const char* data, uint32_t len) {
    char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
    int idx = find_abs(abs);
    if (idx < 0) { idx = vfs_create(abs); if (idx < 0) return -1; }
    if (entries[idx].type != VFS_FILE) return -1;

    uint32_t req_sectors = (len + 511) / 512;

    if (req_sectors > entries[idx].sector_count) {
        // Free old sectors
        for (uint32_t i = 0; i < entries[idx].sector_count; i++) b_clr(entries[idx].start_lba + i);
        
        // Allocate new sectors
        uint32_t new_lba;
        if (find_free_sectors(req_sectors, &new_lba) < 0) return -1;
        for (uint32_t i = 0; i < req_sectors; i++) b_set(new_lba + i);
        
        entries[idx].start_lba = new_lba;
        entries[idx].sector_count = req_sectors;
    }

    // Write to disk
    uint8_t buffer[512];
    uint32_t remaining = len;
    const uint8_t* ptr = (const uint8_t*)data;

    for (uint32_t s = 0; s < req_sectors; s++) {
        k_memset(buffer, 0, 512);
        uint32_t to_write = remaining > 512 ? 512 : remaining;
        k_memcpy(buffer, ptr, to_write);
        ata_write_sector(entries[idx].start_lba + s, buffer);
        ptr += to_write; remaining -= to_write;
    }

    entries[idx].size = len;
    save_mft();
    return len;
}

int vfs_append(const char* path, const char* data, uint32_t len) {
    char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
    int idx = find_abs(abs);
    if (idx < 0) { idx = vfs_create(abs); if (idx < 0) return -1; }
    
    // Simple block append for safety: read old file, append data, rewrite
    static char temp_buf[65536]; // Max append size limits to 64KB 
    uint32_t current_size = entries[idx].size;
    
    if (current_size + len > sizeof(temp_buf)) return -1; 
    
    if (current_size > 0) vfs_read(path, temp_buf, current_size + 1);
    k_memcpy(temp_buf + current_size, data, len);
    
    int written = vfs_write(path, temp_buf, current_size + len);
    return (written > 0) ? len : -1;
}

int vfs_read(const char* path, char* buf, uint32_t buf_len) {
    char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
    int idx = find_abs(abs);
    if (idx < 0 || entries[idx].type != VFS_FILE) return -1;

    uint32_t to_read = entries[idx].size;
    if (to_read > buf_len - 1) to_read = buf_len - 1;

    uint8_t sector_buf[512];
    uint32_t remaining = to_read;
    uint8_t* ptr = (uint8_t*)buf;

    for (uint32_t s = 0; s < entries[idx].sector_count && remaining > 0; s++) {
        ata_read_sector(entries[idx].start_lba + s, sector_buf);
        uint32_t chunk = remaining > 512 ? 512 : remaining;
        k_memcpy(ptr, sector_buf, chunk);
        ptr += chunk; remaining -= chunk;
    }
    
    buf[to_read] = '\0';
    return to_read;
}

int vfs_delete(const char* path) {
    char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
    int idx = find_abs(abs);
    if (idx < 0 || entries[idx].type != VFS_FILE) return -1;
    
    // Free sectors
    for (uint32_t s = 0; s < entries[idx].sector_count; s++) b_clr(entries[idx].start_lba + s);
    
    entries[idx].used = false; 
    save_mft();
    return 0;
}

uint32_t vfs_size(const char* path) {
    char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
    int idx = find_abs(abs);
    return (idx < 0 || entries[idx].type != VFS_FILE) ? 0 : entries[idx].size;
}


//  Directory operations 
int vfs_mkdir(const char* path) {
    char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
    if (k_strcmp(abs, "/") == 0) return 0;   
    if (find_abs(abs) >= 0) return -1;        
    int s = free_slot(); if (s < 0) return -1;
    
    k_strncpy(entries[s].path, abs, VFS_MAX_PATH-1);
    entries[s].type = VFS_DIR; 
    entries[s].size = 0;
    entries[s].start_lba = 0;
    entries[s].sector_count = 0;
    entries[s].used = true;
    save_mft();
    return s;
}

int vfs_rmdir(const char* path) {
    char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
    if (k_strcmp(abs, "/") == 0) return -1;
    int dl = k_strlen(abs);
    for (int i = 0; i < VFS_MAX_ENTRIES; i++) {
        if (!entries[i].used) continue;
        const char* p = entries[i].path;
        if (k_strcmp(p, abs) == 0 ||
            (k_strncmp(p, abs, dl) == 0 && p[dl] == '/')) {
            // Free any files inside the dir before deleting
            if (entries[i].type == VFS_FILE) {
                for (uint32_t s = 0; s < entries[i].sector_count; s++) b_clr(entries[i].start_lba + s);
            }
            entries[i].used = false;
        }
    }
    save_mft();
    return 0;
}

int vfs_chdir(const char* path) {
    if (!path || !path[0]) return -1;
    char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
    if (k_strcmp(abs, "/") == 0) { k_strcpy(vfs_cwd, "/"); return 0; }
    int idx = find_abs(abs);
    if (idx < 0 || entries[idx].type != VFS_DIR) return -1;
    k_strncpy(vfs_cwd, abs, VFS_MAX_PATH-1); return 0;
}


//  Move / copy 
//  Move / copy 
int vfs_mv(const char* src, const char* dst) {
    char asrc[VFS_MAX_PATH], adst[VFS_MAX_PATH];
    vfs_resolve(src, asrc, VFS_MAX_PATH);
    vfs_resolve(dst, adst, VFS_MAX_PATH);

    // Strip trailing slashes to normalize paths
    int slen = k_strlen(asrc);
    while (slen > 1 && asrc[slen - 1] == '/') { asrc[slen - 1] = '\0'; slen--; }
    int dlen = k_strlen(adst);
    while (dlen > 1 && adst[dlen - 1] == '/') { adst[dlen - 1] = '\0'; dlen--; }

    if (k_strcmp(asrc, adst) == 0) return 0;

    int src_idx = find_abs(asrc);
    if (src_idx < 0) return -1; // Source not found

    // Explicitly check if destination is root, OR an existing directory
    bool dst_is_dir = (k_strcmp(adst, "/") == 0);
    int dst_idx = find_abs(adst);
    if (dst_idx >= 0 && entries[dst_idx].type == VFS_DIR) {
        dst_is_dir = true;
    }

    if (dst_is_dir) {
        // Destination is a directory, move inside it
        const char* base = asrc;
        for (int i = slen - 1; i >= 0; i--) {
            if (asrc[i] == '/') { base = asrc + i + 1; break; }
        }
        // Only append a slash if we aren't already at root
        if (k_strcmp(adst, "/") != 0) {
            k_strcat(adst, "/");
        }
        k_strcat(adst, base);
        if (find_abs(adst) >= 0) return -1; // Target already exists inside dir
    } else if (dst_idx >= 0) {
        return -1; // Cannot overwrite existing file
    }

    int sl = k_strlen(asrc);
    for (int i = 0; i < VFS_MAX_ENTRIES; i++) {
        if (!entries[i].used) continue;
        const char* p = entries[i].path;
        if (k_strcmp(p, asrc) == 0) {
            k_strncpy(entries[i].path, adst, VFS_MAX_PATH - 1);
        } else if (k_strncmp(p, asrc, sl) == 0 && p[sl] == '/') {
            char np[VFS_MAX_PATH];
            k_strncpy(np, adst, VFS_MAX_PATH - 1);
            k_strcat(np, p + sl);
            k_strncpy(entries[i].path, np, VFS_MAX_PATH - 1);
        }
    }
    save_mft();
    return 0;
}

int vfs_cp(const char* src, const char* dst) {
    char asrc[VFS_MAX_PATH], adst[VFS_MAX_PATH];
    vfs_resolve(src, asrc, VFS_MAX_PATH);
    vfs_resolve(dst, adst, VFS_MAX_PATH);

    // Strip trailing slashes to normalize paths
    int slen = k_strlen(asrc);
    while (slen > 1 && asrc[slen - 1] == '/') { asrc[slen - 1] = '\0'; slen--; }
    int dlen = k_strlen(adst);
    while (dlen > 1 && adst[dlen - 1] == '/') { adst[dlen - 1] = '\0'; dlen--; }

    if (k_strcmp(asrc, adst) == 0) return -1;

    int src_idx = find_abs(asrc);
    if (src_idx < 0) return -1; // Source not found

    // Explicitly check if destination is root, OR an existing directory
    bool dst_is_dir = (k_strcmp(adst, "/") == 0);
    int dst_idx = find_abs(adst);
    if (dst_idx >= 0 && entries[dst_idx].type == VFS_DIR) {
        dst_is_dir = true;
    }

    if (dst_is_dir) {
        // Destination is a directory, copy inside it
        const char* base = asrc;
        for (int i = slen - 1; i >= 0; i--) {
            if (asrc[i] == '/') { base = asrc + i + 1; break; }
        }
        // Only append a slash if we aren't already at root
        if (k_strcmp(adst, "/") != 0) {
            k_strcat(adst, "/");
        }
        k_strcat(adst, base);
        if (find_abs(adst) >= 0) return -1; // Target already exists inside dir
    } else if (dst_idx >= 0) {
        return -1; // Cannot overwrite existing file
    }

    int sl = k_strlen(asrc);
    int to_copy[VFS_MAX_ENTRIES]; 
    int nc = 0;
    
    for (int i = 0; i < VFS_MAX_ENTRIES; i++) {
        if (!entries[i].used) continue;
        const char* p = entries[i].path;
        if (k_strcmp(p, asrc) == 0 ||
            (k_strncmp(p, asrc, sl) == 0 && p[sl] == '/')) {
            to_copy[nc++] = i;
        }
    }
    
    for (int ci = 0; ci < nc; ci++) {
        int i = to_copy[ci];
        int s = free_slot(); if (s < 0) return -1;
        entries[s] = entries[i];
        
        char np[VFS_MAX_PATH];
        k_strncpy(np, adst, VFS_MAX_PATH - 1);
        if (k_strcmp(entries[i].path, asrc) != 0) {
            k_strcat(np, entries[i].path + sl);
        }
        k_strncpy(entries[s].path, np, VFS_MAX_PATH - 1);
        
        // Physically duplicate file data on disk
        if (entries[s].type == VFS_FILE && entries[s].size > 0) {
            uint32_t new_lba;
            if (find_free_sectors(entries[s].sector_count, &new_lba) == 0) {
                entries[s].start_lba = new_lba;
                uint8_t buffer[512];
                for (uint32_t sec = 0; sec < entries[s].sector_count; sec++) {
                    ata_read_sector(entries[i].start_lba + sec, buffer);
                    ata_write_sector(new_lba + sec, buffer);
                    b_set(new_lba + sec);
                }
            } else {
                entries[s].start_lba = 0; entries[s].sector_count = 0; entries[s].size = 0;
            }
        } else {
            entries[s].start_lba = 0; entries[s].sector_count = 0;
        }
    }
    save_mft();
    return 0;
}


//  Listing 
void vfs_listdir(const char* dir,
                 char names[][VFS_MAX_PATH], bool is_dir_arr[], int* count) {
    *count = 0;
    for (int i = 0; i < VFS_MAX_ENTRIES; i++) {
        if (!entries[i].used) continue;
        const char* nm = direct_child(entries[i].path, dir);
        if (!nm) continue;
        k_strncpy(names[*count], nm, VFS_MAX_PATH-1);
        names[*count][VFS_MAX_PATH-1] = '\0';
        is_dir_arr[*count] = (entries[i].type == VFS_DIR);
        (*count)++;
    }
}

uint32_t vfs_get_disk_size_mb() {
    return MAX_SECTORS / 2048; 
}

uint32_t vfs_get_used_sectors() {
    uint32_t used = 0;
    for (uint32_t i = 0; i < MAX_SECTORS / 32; i++) {
        uint32_t w = disk_bitmap[i];
        while (w) { w &= w - 1; used++; }
    }
    return used;
}