#include "partition.h"

#include "../../../mem/lib/memory.h"
#include "../../../mem/mm/kheap.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct partition_info
{
    block_device_t *parent;
    uint64_t start_lba;
} partition_info_t;

static uint8_t sector[512];

static bool partition_read(block_device_t *dev,
                           uint64_t lba,
                           uint32_t count,
                           void *buffer)
{
    partition_info_t *part = (partition_info_t *)dev->driver_data;
    return block_read(part->parent, part->start_lba + lba, count, buffer);
}

static bool partition_write(block_device_t *dev,
                            uint64_t lba,
                            uint32_t count,
                            const void *buffer)
{
    partition_info_t *part = (partition_info_t *)dev->driver_data;
    return block_write(part->parent, part->start_lba + lba, count, buffer);
}

static bool register_partition(block_device_t *parent,
                               uint32_t index,
                               uint64_t start_lba,
                               uint64_t sector_count)
{
    if (!parent || start_lba == 0 || sector_count == 0)
        return false;

    if (start_lba >= parent->sector_count ||
        sector_count > parent->sector_count - start_lba)
        return false;

    block_device_t *dev = block_device_alloc();

    if (!dev)
        return false;

    partition_info_t *info = (partition_info_t *)kheap_alloc(sizeof(partition_info_t), 16);

    if (!info)
        return false;

    info->parent = parent;
    info->start_lba = start_lba;

    k_strncpy(dev->name, parent->name, sizeof(dev->name));
    uint32_t pos = (uint32_t)k_strlen(dev->name);
    if (pos + 4 < sizeof(dev->name))
    {
        dev->name[pos++] = 'p';
        if (index >= 10)
            dev->name[pos++] = (char)('0' + (index / 10));
        dev->name[pos++] = (char)('0' + (index % 10));
        dev->name[pos] = 0;
    }

    dev->sector_size = parent->sector_size;
    dev->sector_count = sector_count;
    dev->is_partition = true;
    dev->parent = parent;
    dev->parent_lba = start_lba;
    dev->driver_data = info;
    dev->read = partition_read;
    dev->write = parent->write ? partition_write : 0;

    return block_device_register(dev);
}

static uint32_t scan_mbr(block_device_t *dev)
{
    uint32_t found = 0;

    if (!block_read(dev, 0, 1, sector))
        return 0;

    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return 0;

    for (uint32_t i = 0; i < 4; i++)
    {
        const uint8_t *entry = &sector[446 + i * 16];
        uint8_t type = entry[4];
        uint32_t start = k_le32(entry + 8);
        uint32_t count = k_le32(entry + 12);

        if (type == 0 || count == 0)
            continue;

        if (register_partition(dev, i + 1, start, count))
            found++;
    }

    return found;
}

static bool has_gpt(block_device_t *dev)
{
    if (!block_read(dev, 1, 1, sector))
        return false;

    return k_memcmp(sector, "EFI PART", 8) == 0;
}

static uint32_t scan_gpt(block_device_t *dev)
{
    uint32_t found = 0;

    if (!has_gpt(dev))
        return 0;

    uint64_t entries_lba = k_le64(sector + 72);
    uint32_t entry_count = k_le32(sector + 80);
    uint32_t entry_size = k_le32(sector + 84);

    if (entry_size < 128 || entry_size > 512)
        return 0;

    for (uint32_t i = 0; i < entry_count; i++)
    {
        uint64_t lba = entries_lba + ((uint64_t)i * entry_size) / dev->sector_size;
        uint32_t offset = ((uint64_t)i * entry_size) % dev->sector_size;

        if (!block_read(dev, lba, 1, sector))
            break;

        const uint8_t *entry = sector + offset;
        bool empty = true;

        for (uint32_t b = 0; b < 16; b++)
        {
            if (entry[b] != 0)
            {
                empty = false;
                break;
            }
        }

        if (empty)
            continue;

        uint64_t first_lba = k_le64(entry + 32);
        uint64_t last_lba = k_le64(entry + 40);

        if (last_lba < first_lba)
            continue;

        if (register_partition(dev, i + 1, first_lba, last_lba - first_lba + 1))
            found++;
    }

    return found;
}

uint32_t partition_scan_device(block_device_t *dev)
{
    if (!dev || dev->is_partition || dev->sector_size != 512)
        return 0;

    if (!block_read(dev, 0, 1, sector))
        return 0;

    bool protective_mbr = false;

    if (sector[510] == 0x55 && sector[511] == 0xAA)
    {
        for (uint32_t i = 0; i < 4; i++)
        {
            if (sector[446 + i * 16 + 4] == 0xEE)
                protective_mbr = true;
        }
    }

    if (protective_mbr)
        return scan_gpt(dev);

    return scan_mbr(dev);
}

uint32_t partition_scan_all(void)
{
    uint32_t found = 0;
    block_device_t *dev = block_device_first();

    while (dev)
    {
        block_device_t *next = block_device_next(dev);
        found += partition_scan_device(dev);
        dev = next;
    }

    return found;
}
