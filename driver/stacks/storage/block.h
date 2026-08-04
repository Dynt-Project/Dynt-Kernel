#ifndef DRIVER_STACKS_STORAGE_BLOCK_H
#define DRIVER_STACKS_STORAGE_BLOCK_H

#include <stdbool.h>
#include <stdint.h>

#define STORAGE_NAME_MAX 48

typedef struct block_device block_device_t;

typedef bool (*block_read_fn)(block_device_t *dev,
                              uint64_t lba,
                              uint32_t count,
                              void *buffer);

typedef bool (*block_write_fn)(block_device_t *dev,
                               uint64_t lba,
                               uint32_t count,
                               const void *buffer);

struct block_device
{
    char name[STORAGE_NAME_MAX];
    uint64_t sector_count;
    uint32_t sector_size;
    bool removable;
    bool is_partition;
    block_device_t *parent;
    uint64_t parent_lba;
    void *driver_data;
    block_read_fn read;
    block_write_fn write;
    block_device_t *next;
};

#ifdef __cplusplus
extern "C" {
#endif

void block_stack_init(void);
block_device_t *block_device_alloc(void);
bool block_device_register(block_device_t *dev);
uint32_t block_device_count(void);
block_device_t *block_device_first(void);
block_device_t *block_device_next(block_device_t *dev);
bool block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer);
bool block_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buffer);

#ifdef __cplusplus
}
#endif

#endif // DRIVER_STACKS_STORAGE_BLOCK_H
