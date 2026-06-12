//
// vfs_fatfs.c — stdio VFS over FatFs (the SD_CARD==1 file backend).
//
// Provides the same fopen/fread/... symbols the ABI exposes, but backed by FatFs
// on the SD card instead of LittleFS-on-flash (lfs_flash.c, used when SD_CARD==0).
// FILE is an opaque void* (toolkit stdio.h); each open file is a pool slot
// wrapping a FatFs FIL. Mirrors the littlefs VFS shape so apps are unaffected.
//
// Compiled only when SD_CARD==1.
//
#include <stddef.h>
#include "stdio.h"   // toolkit: FILE is void*
#include "ff.h"

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#define VFS_NFILES 4
static struct vfs_fil {
    int used;
    FIL f;
} s_vfiles[VFS_NFILES];

FILE *fopen(const char *path, const char *mode)
{
    struct vfs_fil *vf = 0;
    for (int i = 0; i < VFS_NFILES; i++)
        if (!s_vfiles[i].used) { vf = &s_vfiles[i]; break; }
    if (!vf) return 0;

    BYTE flags;
    if (mode[0] == 'w')      flags = FA_WRITE | FA_CREATE_ALWAYS;
    else if (mode[0] == 'a') flags = FA_WRITE | FA_OPEN_APPEND;
    else                     flags = FA_READ;
    // "r+"/"w+"/"a+" (allow a trailing 'b'): add the missing access bit.
    if (mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+'))
        flags |= FA_READ | FA_WRITE;

    if (f_open(&vf->f, path, flags) != FR_OK) return 0;
    vf->used = 1;
    return (FILE *)vf;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    struct vfs_fil *vf = (struct vfs_fil *)stream;
    if (!vf || !vf->used || size == 0) return 0;
    UINT br = 0;
    if (f_read(&vf->f, ptr, size * nmemb, &br) != FR_OK) return 0;
    return br / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    struct vfs_fil *vf = (struct vfs_fil *)stream;
    if (!vf || !vf->used || size == 0) return 0;
    UINT bw = 0;
    if (f_write(&vf->f, ptr, size * nmemb, &bw) != FR_OK) return 0;
    return bw / size;
}

int fclose(FILE *stream)
{
    struct vfs_fil *vf = (struct vfs_fil *)stream;
    if (!vf || !vf->used) return -1;
    FRESULT r = f_close(&vf->f);
    vf->used = 0;
    return r ? -1 : 0;
}

int fseek(FILE *stream, long offset, int whence)
{
    struct vfs_fil *vf = (struct vfs_fil *)stream;
    if (!vf || !vf->used) return -1;
    FSIZE_t base = (whence == SEEK_CUR) ? f_tell(&vf->f)
                 : (whence == SEEK_END) ? f_size(&vf->f)
                 : 0;
    return f_lseek(&vf->f, base + offset) ? -1 : 0;
}

long ftell(FILE *stream)
{
    struct vfs_fil *vf = (struct vfs_fil *)stream;
    if (!vf || !vf->used) return -1;
    return (long)f_tell(&vf->f);
}

int feof(FILE *stream)
{
    struct vfs_fil *vf = (struct vfs_fil *)stream;
    if (!vf || !vf->used) return 1;
    return f_eof(&vf->f) ? 1 : 0;
}

int ferror(FILE *stream)
{
    struct vfs_fil *vf = (struct vfs_fil *)stream;
    if (!vf || !vf->used) return 1;
    return f_error(&vf->f) ? 1 : 0;
}

int remove(const char *path)
{
    return f_unlink(path) ? -1 : 0;
}
