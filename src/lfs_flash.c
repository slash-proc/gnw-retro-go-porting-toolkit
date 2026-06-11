//
// LittleFS over the external OSPI flash partition (the retro-go-generated FS).
//
// The device already carries a 10 MB LittleFS partition at 0x90A00000..0x91400000
// (block 4096, 2560 blocks), stored "inverted" — block N lives at
// (partition_end - (N+1)*block_size), the same layout retro-go uses (gw_littlefs.c).
// We mount it and expose a small file read/write API so persistence can live in
// real /data/*.sav files (the retro-go storage model), not a private flash region.
//
// Block ops reuse the firmware OSPI driver (deps/Core/Src/flash.c): reads are plain
// XIP memcpy; prog/erase toggle memory-mapped mode (D-cache handled). Mounting only
// reads, so it is safe to bring up before any writes.
//

#include <stdint.h>
#include <string.h>
#include <stdio.h>          // FILE (==void) for the VFS layer

#include "flash.h"          // OSPI_* + stm32h7xx_hal.h (SCB)
#include "lfs.h"

#define EXTFLASH_MMAP_BASE  0x90000000UL
#define LFS_PARTITION_END   0x91400000UL   // top of the partition = lfs "context"
#define LFS_PART_BLOCK_SIZE 4096U
#define LFS_PART_BLOCKS     2560U           // 10 MB / 4096

int lfs_flash_mount(void);
int lfs_flash_write(const char *path, const void *data, uint32_t len);
int lfs_flash_read(const char *path, void *buf, uint32_t max);

static inline int dcache_on(void) { return (SCB->CCR & SCB_CCR_DC_Msk) != 0; }

static int blk_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                    void *buffer, lfs_size_t size) {
    const uint8_t *addr = (const uint8_t *)c->context - ((block + 1) * c->block_size) + off;
    memcpy(buffer, addr, size);
    return 0;
}

static int blk_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                    const void *buffer, lfs_size_t size) {
    uint32_t addr = (uint32_t)c->context - ((block + 1) * c->block_size) + off;
    int dc = dcache_on();
    if (dc) {
        SCB_CleanDCache_by_Addr((uint32_t *)addr, size);
        SCB_InvalidateDCache_by_Addr((uint32_t *)addr, size);
        SCB_DisableDCache();
    }
    OSPI_DisableMemoryMappedMode();
    OSPI_Program(addr - EXTFLASH_MMAP_BASE, buffer, size);
    OSPI_EnableMemoryMappedMode();
    if (dc) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)addr, size);
        SCB_EnableDCache();
    }
    return 0;
}

static int blk_erase(const struct lfs_config *c, lfs_block_t block) {
    uint32_t addr = (uint32_t)c->context - ((block + 1) * c->block_size);
    int dc = dcache_on();
    if (dc) {
        SCB_CleanDCache_by_Addr((uint32_t *)addr, c->block_size);
        SCB_InvalidateDCache_by_Addr((uint32_t *)addr, c->block_size);
        SCB_DisableDCache();
    }
    OSPI_DisableMemoryMappedMode();
    OSPI_EraseSync(addr - EXTFLASH_MMAP_BASE, c->block_size);
    OSPI_EnableMemoryMappedMode();
    if (dc) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)addr, c->block_size);
        SCB_EnableDCache();
    }
    return 0;
}

static int blk_sync(const struct lfs_config *c) { (void)c; return 0; }

static uint8_t s_read_buf[256];
static uint8_t s_prog_buf[256];
static uint8_t s_look_buf[16] __attribute__((aligned(4)));
static uint8_t s_file_buf[256];

static lfs_t s_lfs;
static const struct lfs_config s_cfg = {
    .read = blk_read, .prog = blk_prog, .erase = blk_erase, .sync = blk_sync,
    .read_buffer = s_read_buf, .prog_buffer = s_prog_buf, .lookahead_buffer = s_look_buf,
    .read_size = 256, .prog_size = 256, .cache_size = 256, .lookahead_size = 16,
    .block_size = LFS_PART_BLOCK_SIZE, .block_count = LFS_PART_BLOCKS,
    .block_cycles = 500,
    .context = (void *)LFS_PARTITION_END,
};
static const struct lfs_file_config s_fcfg = { .buffer = s_file_buf };

static int s_mounted = 0;

// SWD-observable bring-up status: [0]=mount rc (0=ok) [1]=used blocks.
volatile int32_t g_lfs_status[2] = { -2, 0 };

int lfs_flash_mount(void) {
    if (s_mounted) return 0;
    int rc = lfs_mount(&s_lfs, &s_cfg);
    if (rc == 0) {
        s_mounted = 1;
        g_lfs_status[0] = 0;
        g_lfs_status[1] = (int32_t)lfs_fs_size(&s_lfs);
    } else {
        g_lfs_status[0] = rc;
    }
    SCB_CleanDCache_by_Addr((uint32_t *)g_lfs_status, sizeof g_lfs_status);
    return rc;
}

// Write `len` bytes to `path`, creating parent dirs. Returns len, or <0 on error.
int lfs_flash_write(const char *path, const void *data, uint32_t len) {
    if (lfs_flash_mount()) return -1;
    // Create every parent directory of `path` (e.g. "/data" for "/data/x").
    // lfs_file_opencfg with LFS_O_CREAT does NOT create missing parents, so a
    // first write to "/data/..." fails with LFS_ERR_NOENT unless we do this.
    // LFS_ERR_EXIST is the normal "already there" case and is fine.
    char tmp[64];
    strncpy(tmp, path, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = 0;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            int mk = lfs_mkdir(&s_lfs, tmp);
            *p = '/';
            if (mk != 0 && mk != LFS_ERR_EXIST)
                return -1;   // real failure creating a parent dir
        }
    }
    lfs_file_t f;
    if (lfs_file_opencfg(&s_lfs, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &s_fcfg))
        return -1;
    lfs_ssize_t w = lfs_file_write(&s_lfs, &f, data, len);
    lfs_file_close(&s_lfs, &f);
    return (w == (lfs_ssize_t)len) ? (int)len : -1;
}

// Remove a file (clear a slot). Ignores "not found".
int lfs_flash_remove(const char *path) {
    if (lfs_flash_mount()) return -1;
    return lfs_remove(&s_lfs, path);
}

// Read up to `max` bytes from `path`. Returns bytes read, or <0 if absent/error.
int lfs_flash_read(const char *path, void *buf, uint32_t max) {
    if (lfs_flash_mount()) return -1;
    lfs_file_t f;
    if (lfs_file_opencfg(&s_lfs, &f, path, LFS_O_RDONLY, &s_fcfg))
        return -1;
    lfs_ssize_t r = lfs_file_read(&s_lfs, &f, buf, max);
    lfs_file_close(&s_lfs, &f);
    return (r < 0) ? -1 : (int)r;
}

// ---------------------------------------------------------------------------
// stdio (the retro-go plugin VFS shape) over littlefs. Apps see fopen/fread/...
// and never littlefs. FILE is void* (toolkit include/stdio.h); each open file
// is a pool slot wrapping an lfs_file_t with its own cache (LFS_NO_MALLOC). Apps
// typically open one save at a time, so a tiny pool is plenty.
// ---------------------------------------------------------------------------
#define VFS_NFILES 4
static struct vfs_file {
    int                    used;
    lfs_file_t             f;
    struct lfs_file_config fcfg;
    uint8_t                cache[256];
} s_vfiles[VFS_NFILES];

static void vfs_mkparents(const char *path) {
    char tmp[64];
    strncpy(tmp, path, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = 0;
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; lfs_mkdir(&s_lfs, tmp); *p = '/'; }
}

FILE *fopen(const char *path, const char *mode) {
    if (lfs_flash_mount()) return 0;
    struct vfs_file *vf = 0;
    for (int i = 0; i < VFS_NFILES; i++)
        if (!s_vfiles[i].used) { vf = &s_vfiles[i]; break; }
    if (!vf) return 0;
    int flags;
    if (mode[0] == 'w')      { flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;  vfs_mkparents(path); }
    else if (mode[0] == 'a') { flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND; vfs_mkparents(path); }
    else                       flags = LFS_O_RDONLY;
    memset(&vf->fcfg, 0, sizeof vf->fcfg);
    vf->fcfg.buffer = vf->cache;
    if (lfs_file_opencfg(&s_lfs, &vf->f, path, flags, &vf->fcfg)) return 0;
    vf->used = 1;
    return (FILE *)vf;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    struct vfs_file *vf = (struct vfs_file *)stream;
    if (!vf || !vf->used || size == 0) return 0;
    lfs_ssize_t r = lfs_file_read(&s_lfs, &vf->f, ptr, size * nmemb);
    return (r < 0) ? 0 : (size_t)r / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    struct vfs_file *vf = (struct vfs_file *)stream;
    if (!vf || !vf->used || size == 0) return 0;
    lfs_ssize_t w = lfs_file_write(&s_lfs, &vf->f, ptr, size * nmemb);
    return (w < 0) ? 0 : (size_t)w / size;
}

int fclose(FILE *stream) {
    struct vfs_file *vf = (struct vfs_file *)stream;
    if (!vf || !vf->used) return -1;
    int rc = lfs_file_close(&s_lfs, &vf->f);
    vf->used = 0;
    return rc ? -1 : 0;
}

int remove(const char *path) {
    if (lfs_flash_mount()) return -1;
    int rc = lfs_remove(&s_lfs, path);
    return (rc == 0 || rc == LFS_ERR_NOENT) ? 0 : -1;
}
