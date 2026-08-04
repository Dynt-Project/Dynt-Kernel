#ifndef FS_VFS_H
#define FS_VFS_H

#include "../driver/stacks/storage/block.h"

#include <stdbool.h>
#include <stdint.h>

#define VFS_NAME_MAX 32

// open() flags, Linux-compatible values (see abi-bits/fcntl.h)
#define VFS_O_RDONLY 0
#define VFS_O_WRONLY 1
#define VFS_O_RDWR   2
#define VFS_O_CREAT  0100
#define VFS_O_TRUNC  01000
#define VFS_O_APPEND 02000

// lseek() whence values
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

typedef struct process process_t;

typedef struct vfs_mount vfs_mount_t;

typedef bool (*vfs_probe_fn)(block_device_t *dev);
typedef bool (*vfs_mount_fn)(block_device_t *dev, void **out_ctx);
typedef int32_t (*vfs_read_file_fn)(void *ctx, const char *path,
                                    void *buffer, uint32_t buffer_size);
typedef bool (*vfs_write_file_fn)(void *ctx, const char *path,
                                  const void *buffer, uint32_t size);
typedef bool (*vfs_stat_file_fn)(void *ctx, const char *path,
                                 uint64_t *size, bool *is_dir);
typedef int32_t (*vfs_read_at_fn)(void *ctx, const char *path,
                                  uint32_t offset, void *buffer,
                                  uint32_t buffer_size);
typedef bool (*vfs_write_at_fn)(void *ctx, const char *path,
                                uint32_t offset, const void *buffer,
                                uint32_t size, uint32_t *out_new_size);
typedef void (*vfs_list_dir_fn)(void *ctx, const char *path,
                                void (*callback)(const char *name,
                                                 uint32_t size,
                                                 bool is_dir,
                                                 void *user),
                                void *user);

typedef struct vfs_fs_type
{
    const char *name;
    vfs_probe_fn probe;
    vfs_mount_fn mount;
    vfs_read_file_fn read_file;
    vfs_write_file_fn write_file;
    vfs_stat_file_fn stat_file;
    vfs_read_at_fn read_at;
    vfs_write_at_fn write_at;
    vfs_list_dir_fn list_dir;
    struct vfs_fs_type *next;
} vfs_fs_type_t;

struct vfs_mount
{
    char path[VFS_NAME_MAX];
    const vfs_fs_type_t *fs;
    block_device_t *dev;
    void *fs_data;
    vfs_mount_t *next;
};

#ifdef __cplusplus
extern "C" {
#endif

void vfs_init(void);
bool vfs_register_fs(vfs_fs_type_t *fs);
uint32_t vfs_mount_all(void);
uint32_t vfs_mount_count(void);
vfs_mount_t *vfs_mount_first(void);
vfs_mount_t *vfs_find_mount(const char *path);

int32_t vfs_read_file(const char *path, void *buffer, uint32_t buffer_size);
bool vfs_write_file(const char *path, const void *buffer, uint32_t size);
void vfs_list_dir(const char *path,
                  void (*callback)(const char *name,
                                   uint32_t size,
                                   bool is_dir,
                                   void *user),
                  void *user);

// fd-based file I/O (Linux open/read/write/lseek/close ABI, negative =
// error). the per-process fd table lives in process_t.
int32_t vfs_open_fd(process_t *proc, const char *path, uint32_t flags);
int64_t vfs_read_fd(process_t *proc, int32_t fd, void *buf, uint64_t len);
int64_t vfs_write_fd(process_t *proc, int32_t fd, const void *buf,
                     uint64_t len);
int64_t vfs_seek_fd(process_t *proc, int32_t fd, int64_t off,
                    uint32_t whence);
int32_t vfs_close_fd(process_t *proc, int32_t fd);
int32_t vfs_stat(const char *path, uint64_t *size, bool *is_dir);

#ifdef __cplusplus
}
#endif

#endif /* FS_VFS_H */