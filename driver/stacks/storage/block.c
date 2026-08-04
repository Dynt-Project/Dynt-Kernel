#include "block.h"

#include "../../../mem/lib/memory.h"
#include "../../../mem/mm/kheap.h"

static block_device_t *first_device;
static block_device_t *last_device;
static uint32_t device_count;

void block_stack_init(void)
{
    first_device = 0;
    last_device = 0;
    device_count = 0;
}

block_device_t *block_device_alloc(void)
{
    block_device_t *dev = (block_device_t *)kheap_alloc(sizeof(block_device_t), 16);

    if (dev)
        dev->sector_size = 512;

    return dev;
}

bool block_device_register(block_device_t *dev)
{
    if (!dev || !dev->read || dev->sector_size == 0 || dev->sector_count == 0)
        return false;

    dev->next = 0;

    if (!first_device)
        first_device = dev;
    else
        last_device->next = dev;

    last_device = dev;
    device_count++;
    return true;
}

uint32_t block_device_count(void)
{
    return device_count;
}

block_device_t *block_device_first(void)
{
    return first_device;
}

block_device_t *block_device_next(block_device_t *dev)
{
    return dev ? dev->next : 0;
}

bool block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer)
{
    if (!dev || !dev->read || !buffer || count == 0)
        return false;

    if (lba >= dev->sector_count || count > dev->sector_count - lba)
        return false;

    return dev->read(dev, lba, count, buffer);
}

bool block_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buffer)
{
    if (!dev || !dev->write || !buffer || count == 0)
        return false;

    if (lba >= dev->sector_count || count > dev->sector_count - lba)
        return false;

    return dev->write(dev, lba, count, buffer);
}
