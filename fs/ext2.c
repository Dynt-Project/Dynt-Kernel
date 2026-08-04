#include "ext2.h"

#include "vfs.h"
#include "../mem/lib/memory.h"

static uint8_t ext2_sector[1024];

static bool ext2_probe(block_device_t *dev)
{
    if (!dev || dev->sector_size != 512)
        return false;

    if (!block_read(dev, 2, 2, ext2_sector))
        return false;

    return k_le16(ext2_sector + 56) == 0xEF53;
}

void ext2_register(void)
{
    static vfs_fs_type_t fs;

    fs.name = "ext2";
    fs.probe = ext2_probe;
    vfs_register_fs(&fs);
}
