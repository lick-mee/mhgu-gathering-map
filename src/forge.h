// Forge API 1.5.1 as the MHGU Recomp exposes it (signatures worked out from the port and an existing plugin).
#pragma once

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef int s32;
typedef _Bool bool;
#define true 1
#define false 0
#define NULL ((void *)0)

#define EXPORT __attribute__((visibility("default")))

typedef struct {
    u16 major, minor, patch;
    char name[64];
} ForgePluginInfo;

// Filled by forge_hook_create: the hooked address, our replacement, and a callable trampoline to the original.
typedef struct {
    u32 target;
    u32 replacement;
    u32 original;
    u32 reserved[3];
} ForgeHook;

enum { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };

extern void forge_hook_create(ForgeHook *hook, u32 target, u32 replacement, u32 *original);
extern u32 forge_mem_getMainTextAddr(void);
extern void forge_log(int level, const char *fmt, ...);
extern u32 forge_singleton_getInstanceByName(const char *name);
extern u32 forge_singleton_getAllInstances(u32 *out, u32 max);
extern bool forge_input_isKeyPressed(u32 scancode);
extern bool forge_fs_mountSaveData(void); // the game's save storage; check the result before using it
extern void forge_fs_unmountSaveData(void);
extern const char *forge_fs_getSaveDataMountPoint(void); // usually "savedata"
extern bool forge_fs_fileExists(const char *path);
extern void *malloc(u32 size);

// nn::fs from the game's SDK (Result is 0 on success).
typedef long long s64;
typedef struct { void *handle; } FsFile;
typedef struct { int flags; } FsWriteOption; // 1: flush
enum { FS_READ = 1, FS_WRITE = 2, FS_APPEND = 4 };
extern u32 nnfs_OpenFile(FsFile *out, const char *path, int mode) __asm__("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci");
extern void nnfs_CloseFile(FsFile f) __asm__("_ZN2nn2fs9CloseFileENS0_10FileHandleE");
extern u32 nnfs_GetFileSize(s64 *out, FsFile f) __asm__("_ZN2nn2fs11GetFileSizeEPxNS0_10FileHandleE");
extern u32 nnfs_ReadFile(FsFile f, s64 off, void *buf, u32 size) __asm__("_ZN2nn2fs8ReadFileENS0_10FileHandleExPvj");
extern u32 nnfs_CreateFile(const char *path, s64 size) __asm__("_ZN2nn2fs10CreateFileEPKcx");
extern u32 nnfs_SetFileSize(FsFile f, s64 size) __asm__("_ZN2nn2fs11SetFileSizeENS0_10FileHandleEx");
extern u32 nnfs_WriteFile(FsFile f, s64 off, const void *buf, u32 size, const FsWriteOption *opt)
    __asm__("_ZN2nn2fs9WriteFileENS0_10FileHandleExPKvjRKNS0_11WriteOptionE");
extern u32 nnfs_FlushFile(FsFile f) __asm__("_ZN2nn2fs9FlushFileENS0_10FileHandleE");
extern u32 nnfs_CommitSaveData(const char *mount) __asm__("_ZN2nn2fs14CommitSaveDataEPKc");
extern void free(void *p);

// cimgui, as far as this plugin needs it.
extern void igSetCurrentContext(void *ctx);
extern void igSetAllocatorFunctions(void *alloc, void *free, void *user);
extern bool igTreeNode_Str(const char *label);
extern void igTreePop(void);
extern void igTextUnformatted(const char *text, const char *text_end);
extern bool igCheckbox(const char *label, bool *v);
extern bool igIsItemDeactivatedAfterEdit(void);
extern bool igSliderInt(const char *label, int *v, int v_min, int v_max, const char *format, int flags);
