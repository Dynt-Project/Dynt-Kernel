#ifndef FS_FAT32_H
#define FS_FAT32_H

#include "../driver/stacks/storage/block.h"

#include <stdbool.h>
#include <stdint.h>

#define FAT32_NAME_MAX 13   /* 8.3 + NUL */

typedef struct fat32_ctx
{
    block_device_t *dev;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint8_t  fat_count;
    uint32_t fat_size;
    uint32_t root_cluster;
    uint32_t data_start;
    uint32_t fat_start;
    uint32_t bytes_per_cluster;
} fat32_ctx_t;

typedef struct fat32_dirent
{
    char     name[FAT32_NAME_MAX];
    uint32_t size;
    uint32_t first_cluster;
    bool     is_dir;
} fat32_dirent_t;

#ifdef __cplusplus
extern "C" {
#endif

void fat32_register(void);
bool fat32_probe(block_device_t *dev);
bool fat32_mount(block_device_t *dev, fat32_ctx_t **out_ctx);
bool fat32_open(fat32_ctx_t *ctx, const char *name, fat32_dirent_t *out);
uint32_t fat32_read(fat32_ctx_t *ctx, uint32_t start_cluster,
                    void *buffer, uint32_t buffer_size);
uint32_t fat32_read_at(fat32_ctx_t *ctx, uint32_t start_cluster,
                       uint32_t offset, void *buffer, uint32_t buffer_size);
bool fat32_write_file(fat32_ctx_t *ctx, const char *path,
                      const void *buffer, uint32_t size);
uint32_t fat32_write(fat32_ctx_t *ctx, uint32_t *first_cluster,
                     uint32_t offset, const void *buffer, uint32_t size);
bool fat32_update_size(fat32_ctx_t *ctx, const char *path, uint32_t size);
bool fat32_stat(fat32_ctx_t *ctx, const char *path,
                uint64_t *size, bool *is_dir);
void fat32_list(fat32_ctx_t *ctx,
                void (*callback)(const fat32_dirent_t *entry, void *user),
                void *user);

#ifdef __cplusplus
}
#endif

#endif /* FS_FAT32_H */