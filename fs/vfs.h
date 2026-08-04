#ifndef FS_VFS_H
#define FS_VFS_H

#include "../driver/stacks/storage/block.h"

#include <stdbool.h>
#include <stdint.h>

#define VFS_NAME_MAX 32

typedef struct vfs_mount vfs_mount_t;

typedef bool (*vfs_probe_fn)(block_device_t *dev);
typedef bool (*vfs_mount_fn)(block_device_t *dev, void **out_ctx);
typedef int32_t (*vfs_read_file_fn)(void *ctx, const char *path,
                                    void *buffer, uint32_t buffer_size);
typedef bool (*vfs_write_file_fn)(void *ctx, const char *path,
                                  const void *buffer, uint32_t size);
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

#ifdef __cplusplus
}
#endif

#endif /* FS_VFS_H */