#include "ide.h"

#include "../../../../arch/x86_64/io/io.h"
#include "../../../../driver/stacks/storage/block.h"
#include "../../../../mem/lib/memory.h"

#include <stdbool.h>
#include <stdint.h>

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_SECONDARY_IO 0x170
#define ATA_SECONDARY_CTRL 0x376

#define ATA_REG_DATA 0x00
#define ATA_REG_ERROR 0x01
#define ATA_REG_SECCOUNT0 0x02
#define ATA_REG_LBA0 0x03
#define ATA_REG_LBA1 0x04
#define ATA_REG_LBA2 0x05
#define ATA_REG_HDDEVSEL 0x06
#define ATA_REG_COMMAND 0x07
#define ATA_REG_STATUS 0x07

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_READ_PIO_EXT 0x24
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_WRITE_PIO_EXT 0x34
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA

#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

typedef struct ide_channel
{
    uint16_t io;
    uint16_t ctrl;
} ide_channel_t;

typedef struct ide_drive
{
    ide_channel_t channel;
    uint8_t drive;
    bool lba48;
    uint64_t sectors;
} ide_drive_t;

static ide_drive_t drive_data[4];
static uint32_t drive_data_count;

static uint8_t ide_read_reg(const ide_channel_t *channel, uint8_t reg)
{
    return inb((uint16_t)(channel->io + reg));
}

static void ide_write_reg(const ide_channel_t *channel, uint8_t reg, uint8_t value)
{
    outb((uint16_t)(channel->io + reg), value);
}

static void ide_delay(const ide_channel_t *channel)
{
    for (int i = 0; i < 4; i++)
        inb((uint16_t)(channel->ctrl));
}

static bool ide_wait(const ide_channel_t *channel, bool drq)
{
    for (uint32_t i = 0; i < 1000000; i++)
    {
        uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);

        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return false;

        if (!(status & ATA_SR_BSY))
        {
            if (!drq || (status & ATA_SR_DRQ))
                return true;
        }
    }

    return false;
}

static void ide_select_drive(const ide_channel_t *channel, uint8_t drive)
{
    /* 0xE0 sets the LBA bit (bit 6) — required for both LBA28 and LBA48
       commands.  The old 0xA0 value left the drive in CHS mode, which
       caused all PIO reads/writes to fail under QEMU. */
    ide_write_reg(channel, ATA_REG_HDDEVSEL, (uint8_t)(0xE0 | (drive << 4)));
    ide_delay(channel);
}

static void ata_string(char *dst, const uint16_t *id, uint32_t word,
                       uint32_t words, uint32_t dst_size)
{
    uint32_t pos = 0;

    for (uint32_t i = 0; i < words && pos + 1 < dst_size; i++)
    {
        uint16_t value = id[word + i];
        dst[pos++] = (char)(value >> 8);

        if (pos + 1 < dst_size)
            dst[pos++] = (char)(value & 0xFF);
    }

    while (pos > 0 && dst[pos - 1] == ' ')
        pos--;

    dst[pos] = 0;
}

static bool ide_identify(const ide_channel_t *channel,
                         uint8_t drive,
                         uint16_t identify[256])
{
    ide_select_drive(channel, drive);
    ide_write_reg(channel, ATA_REG_SECCOUNT0, 0);
    ide_write_reg(channel, ATA_REG_LBA0, 0);
    ide_write_reg(channel, ATA_REG_LBA1, 0);
    ide_write_reg(channel, ATA_REG_LBA2, 0);
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    if (ide_read_reg(channel, ATA_REG_STATUS) == 0)
        return false;

    if (!ide_wait(channel, true))
        return false;

    insw((uint16_t)(channel->io + ATA_REG_DATA), identify, 256);
    return true;
}

static bool ide_pio_access(ide_drive_t *drive,
                           bool write,
                           uint64_t lba,
                           uint32_t count,
                           void *buffer)
{
    uint8_t *bytes = (uint8_t *)buffer;

    while (count)
    {
        uint8_t chunk = count > 255 ? 255 : (uint8_t)count;

        if (!drive->lba48 && (lba + chunk) > 0x10000000ULL)
            return false;

        ide_select_drive(&drive->channel, drive->drive);

        if (drive->lba48)
        {
            ide_write_reg(&drive->channel, ATA_REG_SECCOUNT0, 0);
            ide_write_reg(&drive->channel, ATA_REG_LBA0, (uint8_t)(lba >> 24));
            ide_write_reg(&drive->channel, ATA_REG_LBA1, (uint8_t)(lba >> 32));
            ide_write_reg(&drive->channel, ATA_REG_LBA2, (uint8_t)(lba >> 40));
            ide_write_reg(&drive->channel, ATA_REG_SECCOUNT0, chunk);
            ide_write_reg(&drive->channel, ATA_REG_LBA0, (uint8_t)lba);
            ide_write_reg(&drive->channel, ATA_REG_LBA1, (uint8_t)(lba >> 8));
            ide_write_reg(&drive->channel, ATA_REG_LBA2, (uint8_t)(lba >> 16));
            ide_write_reg(&drive->channel, ATA_REG_COMMAND,
                          write ? ATA_CMD_WRITE_PIO_EXT : ATA_CMD_READ_PIO_EXT);
        }
        else
        {
            ide_write_reg(&drive->channel, ATA_REG_HDDEVSEL,
                          (uint8_t)(0xE0 | (drive->drive << 4) | ((lba >> 24) & 0x0F)));
            ide_write_reg(&drive->channel, ATA_REG_SECCOUNT0, chunk);
            ide_write_reg(&drive->channel, ATA_REG_LBA0, (uint8_t)lba);
            ide_write_reg(&drive->channel, ATA_REG_LBA1, (uint8_t)(lba >> 8));
            ide_write_reg(&drive->channel, ATA_REG_LBA2, (uint8_t)(lba >> 16));
            ide_write_reg(&drive->channel, ATA_REG_COMMAND,
                          write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);
        }

        for (uint32_t sector = 0; sector < chunk; sector++)
        {
            if (!ide_wait(&drive->channel, true))
                return false;

            if (write)
                outsw((uint16_t)(drive->channel.io + ATA_REG_DATA), bytes, 256);
            else
                insw((uint16_t)(drive->channel.io + ATA_REG_DATA), bytes, 256);

            bytes += 512;
        }

        if (write)
        {
            ide_write_reg(&drive->channel, ATA_REG_COMMAND,
                          drive->lba48 ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
            if (!ide_wait(&drive->channel, false))
                return false;
        }

        lba += chunk;
        count -= chunk;
    }

    return true;
}

static bool ide_block_read(block_device_t *dev,
                           uint64_t lba,
                           uint32_t count,
                           void *buffer)
{
    return ide_pio_access((ide_drive_t *)dev->driver_data, false, lba, count, buffer);
}

static bool ide_block_write(block_device_t *dev,
                            uint64_t lba,
                            uint32_t count,
                            const void *buffer)
{
    return ide_pio_access((ide_drive_t *)dev->driver_data, true, lba, count, (void *)buffer);
}

uint32_t ide_init(void)
{
    const ide_channel_t channels[] = {
        { ATA_PRIMARY_IO, ATA_PRIMARY_CTRL },
        { ATA_SECONDARY_IO, ATA_SECONDARY_CTRL },
    };
    uint32_t registered = 0;

    drive_data_count = 0;

    for (uint32_t c = 0; c < 2; c++)
    {
        outb(channels[c].ctrl, 0x02);

        for (uint8_t d = 0; d < 2; d++)
        {
            uint16_t id[256];

            if (!ide_identify(&channels[c], d, id))
                continue;

            if (!(id[49] & (1u << 9)))
                continue;

            if (drive_data_count >= sizeof(drive_data) / sizeof(drive_data[0]))
                continue;

            ide_drive_t *drive = &drive_data[drive_data_count++];
            block_device_t *dev = block_device_alloc();

            if (!dev)
                continue;

            drive->channel = channels[c];
            drive->drive = d;
            drive->lba48 = (id[83] & (1u << 10)) != 0;
            drive->sectors = drive->lba48
                ? ((uint64_t)id[100] |
                   ((uint64_t)id[101] << 16) |
                   ((uint64_t)id[102] << 32) |
                   ((uint64_t)id[103] << 48))
                : ((uint32_t)id[60] | ((uint32_t)id[61] << 16));

            dev->sector_size = 512;
            dev->sector_count = drive->sectors;
            dev->driver_data = drive;
            dev->read = ide_block_read;
            dev->write = ide_block_write;

            k_strncpy(dev->name, "ide", sizeof(dev->name));
            dev->name[3] = (char)('0' + c);
            dev->name[4] = d ? 's' : 'm';
            dev->name[5] = 0;

            char model[41];
            ata_string(model, id, 27, 20, sizeof(model));

            if (block_device_register(dev))
                registered++;
        }
    }

    return registered;
}
