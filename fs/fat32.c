#include "fat32.h"

#include "vfs.h"
#include "../mem/lib/memory.h"
#include "../mem/mm/kheap.h"

#define FAT32_EOC 0x0FFFFFF8
#define FAT32_BAD 0x0FFFFFF7

#define FAT32_DIR_ATTR_DIRECTORY 0x10
#define FAT32_DIR_ATTR_LFN 0x0F

static uint8_t fat_sector[512];

bool fat32_probe(block_device_t *dev)
{
    if (!dev || dev->sector_size != 512)
        return false;

    if (!block_read(dev, 0, 1, fat_sector))
        return false;

    if (fat_sector[510] != 0x55 || fat_sector[511] != 0xAA)
        return false;

    if (k_memcmp(fat_sector + 82, "FAT32   ", 8) == 0)
        return true;

    uint16_t reserved = k_le16(fat_sector + 14);
    uint8_t fats = fat_sector[16];
    uint32_t fat_size = k_le32(fat_sector + 36);
    uint32_t root_cluster = k_le32(fat_sector + 44);

    return reserved != 0 && fats != 0 && fat_size != 0 && root_cluster >= 2;
}

bool fat32_mount(block_device_t *dev, fat32_ctx_t **out_ctx)
{
    if (!dev || !out_ctx)
        return false;

    fat32_ctx_t *ctx = (fat32_ctx_t *)kheap_alloc(sizeof(fat32_ctx_t), 16);

    if (!ctx)
        return false;

    k_memset(ctx, 0, sizeof(*ctx));

    if (!block_read(dev, 0, 1, fat_sector))
        return false;

    uint16_t bytes_per_sector = k_le16(fat_sector + 11);
    uint8_t sectors_per_cluster = fat_sector[13];

    if (bytes_per_sector != 512)
        return false;

    if (sectors_per_cluster == 0 ||
        (sectors_per_cluster & (sectors_per_cluster - 1)) != 0)
        return false;

    ctx->dev = dev;
    ctx->bytes_per_sector = bytes_per_sector;
    ctx->sectors_per_cluster = sectors_per_cluster;
    ctx->reserved_sectors = k_le16(fat_sector + 14);
    ctx->fat_count = fat_sector[16];
    ctx->fat_size = k_le32(fat_sector + 36);
    ctx->root_cluster = k_le32(fat_sector + 44);
    ctx->bytes_per_cluster = bytes_per_sector * sectors_per_cluster;

    if (ctx->fat_count == 0 || ctx->fat_size == 0 || ctx->root_cluster < 2)
        return false;

    ctx->fat_start = ctx->reserved_sectors;
    ctx->data_start = ctx->reserved_sectors + (uint32_t)ctx->fat_count * ctx->fat_size;

    *out_ctx = ctx;
    return true;
}

static uint32_t cluster_to_lba(const fat32_ctx_t *ctx, uint32_t cluster)
{
    return ctx->data_start + ((cluster - 2) * (uint32_t)ctx->sectors_per_cluster);
}

static bool cluster_valid(uint32_t cluster)
{
    return cluster >= 2 && cluster < FAT32_BAD;
}

static uint32_t fat_read_entry(const fat32_ctx_t *ctx, uint32_t cluster)
{
    uint32_t entry_lba = ctx->fat_start + (cluster * 4) / ctx->bytes_per_sector;
    uint32_t entry_off = (cluster * 4) % ctx->bytes_per_sector;

    if (!block_read(ctx->dev, entry_lba, 1, fat_sector))
        return FAT32_BAD;

    return k_le32(fat_sector + entry_off) & 0x0FFFFFFF;
}

uint32_t fat32_read(fat32_ctx_t *ctx, uint32_t start_cluster,
                    void *buffer, uint32_t buffer_size)
{
    if (!ctx || !buffer || buffer_size == 0)
        return 0;

    uint8_t *dst = (uint8_t *)buffer;
    uint32_t total = 0;
    uint32_t cluster = start_cluster;

    while (cluster_valid(cluster) && total < buffer_size)
    {
        uint32_t chunk = ctx->bytes_per_cluster;
        uint32_t left = buffer_size - total;

        if (chunk > left)
            chunk = left;

        uint32_t sectors = chunk / ctx->bytes_per_sector;

        if (sectors > 0 &&
            !block_read(ctx->dev, cluster_to_lba(ctx, cluster), sectors, dst + total))
            break;

        total += chunk;
        cluster = fat_read_entry(ctx, cluster);
    }

    return total;
}

static void build_short_name(const uint8_t *entry, char *out, uint32_t out_size)
{
    uint32_t pos = 0;

    for (uint32_t i = 0; i < 8 && pos + 1 < out_size; i++)
    {
        uint8_t c = entry[i];

        if (c == ' ')
            break;

        out[pos++] = (char)((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
    }

    if (entry[8] != ' ')
    {
        if (pos + 1 < out_size)
            out[pos++] = '.';

        for (uint32_t i = 0; i < 3 && pos + 1 < out_size; i++)
        {
            uint8_t c = entry[8 + i];

            if (c == ' ')
                break;

            out[pos++] = (char)((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
        }
    }

    out[pos] = 0;
}

static bool name_matches(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = *a++;
        char cb = *b++;

        if (ca >= 'a' && ca <= 'z')
            ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z')
            cb = (char)(cb - 'a' + 'A');

        if (ca != cb)
            return false;
    }

    return *a == 0 && *b == 0;
}

static bool entry_to_dirent(const uint8_t *entry, fat32_dirent_t *out)
{
    if (entry[0] == 0x00 || entry[0] == 0xE5)
        return false;

    uint8_t attr = entry[11];

    if ((attr & FAT32_DIR_ATTR_LFN) == FAT32_DIR_ATTR_LFN)
        return false;

    k_memset(out, 0, sizeof(*out));
    build_short_name(entry, out->name, sizeof(out->name));
    out->size = k_le32(entry + 28);
    out->first_cluster = k_le16(entry + 20) << 16 | k_le16(entry + 26);
    out->is_dir = (attr & FAT32_DIR_ATTR_DIRECTORY) != 0;

    if (out->is_dir)
        out->size = 0;

    return true;
}

static bool read_directory(fat32_ctx_t *ctx, uint32_t dir_cluster,
                           void (*visit)(const fat32_ctx_t *ctx,
                                         const uint8_t *entry,
                                         void *user),
                           void *user)
{
    uint8_t *buf = (uint8_t *)kheap_alloc(ctx->bytes_per_cluster, 16);

    if (!buf)
        return false;

    uint32_t cluster = dir_cluster;
    bool found = false;

    while (cluster_valid(cluster))
    {
        if (!block_read(ctx->dev, cluster_to_lba(ctx, cluster),
                        ctx->sectors_per_cluster, buf))
            break;

        uint32_t count = ctx->bytes_per_cluster / 32;

        for (uint32_t i = 0; i < count; i++)
        {
            const uint8_t *entry = buf + i * 32;

            if (entry[0] == 0x00)
            {
                found = true;
                break;
            }

            if (entry[0] == 0xE5)
                continue;

            visit(ctx, entry, user);
        }

        if (found)
            break;

        cluster = fat_read_entry(ctx, cluster);
    }

    kheap_free(buf);
    return true;
}

typedef struct open_search
{
    const char *name;
    fat32_dirent_t *out;
    bool found;
} open_search_t;

static void open_visit(const fat32_ctx_t *ctx, const uint8_t *entry, void *user)
{
    (void)ctx;
    open_search_t *s = (open_search_t *)user;

    if (s->found)
        return;

    fat32_dirent_t d;

    if (entry_to_dirent(entry, &d) && name_matches(d.name, s->name))
    {
        *s->out = d;
        s->found = true;
    }
}

static bool find_entry(fat32_ctx_t *ctx, uint32_t dir_cluster,
                       const char *name, fat32_dirent_t *out)
{
    open_search_t search;
    k_memset(&search, 0, sizeof(search));
    search.name = name;
    search.out = out;

    read_directory(ctx, dir_cluster, open_visit, &search);
    return search.found;
}

static bool split_component(const char **path, char *out, uint32_t out_size)
{
    const char *p = *path;

    while (*p == '/')
        p++;

    if (*p == 0)
        return false;

    uint32_t pos = 0;

    while (*p && *p != '/')
    {
        if (pos + 1 < out_size)
            out[pos++] = *p;
        p++;
    }

    out[pos] = 0;
    *path = p;
    return pos != 0;
}

bool fat32_open(fat32_ctx_t *ctx, const char *name, fat32_dirent_t *out)
{
    if (!ctx || !name || !out)
        return false;

    char component[FAT32_NAME_MAX];
    uint32_t dir_cluster = ctx->root_cluster;
    const char *path = name;
    fat32_dirent_t entry;

    while (split_component(&path, component, sizeof(component)))
    {
        if (!find_entry(ctx, dir_cluster, component, &entry))
            return false;

        if (*path == 0)
        {
            *out = entry;
            return true;
        }

        if (!entry.is_dir)
            return false;

        dir_cluster = entry.first_cluster;
    }

    return false;
}

typedef struct list_search
{
    void (*callback)(const char *name, uint32_t size, bool is_dir, void *user);
    void *user;
} list_search_t;

static void list_visit(const fat32_ctx_t *ctx, const uint8_t *entry, void *user)
{
    (void)ctx;
    list_search_t *s = (list_search_t *)user;
    fat32_dirent_t d;

    if (entry_to_dirent(entry, &d))
        s->callback(d.name, d.size, d.is_dir, s->user);
}

static void fat32_list_dir_ctx(fat32_ctx_t *ctx, const char *path,
                               void (*callback)(const char *name,
                                                uint32_t size,
                                                bool is_dir,
                                                void *user),
                               void *user)
{
    if (!ctx || !callback)
        return;

    uint32_t dir_cluster = ctx->root_cluster;

    if (path && path[0] != 0 && k_strncmp(path, "/", 2) != 0)
    {
        fat32_dirent_t entry;

        if (fat32_open(ctx, path, &entry) && entry.is_dir)
            dir_cluster = entry.first_cluster;
        else
            return;
    }

    list_search_t s;
    s.callback = callback;
    s.user = user;
    read_directory(ctx, dir_cluster, list_visit, &s);
}

static int32_t fat32_vfs_read_file(void *vctx, const char *path,
                                   void *buffer, uint32_t buffer_size)
{
    fat32_ctx_t *ctx = (fat32_ctx_t *)vctx;

    if (!ctx || !buffer || !path)
        return -1;

    fat32_dirent_t entry;

    if (!fat32_open(ctx, path, &entry) || entry.is_dir)
        return -1;

    return (int32_t)fat32_read(ctx, entry.first_cluster, buffer, buffer_size);
}

static void fat32_vfs_list_dir(void *vctx, const char *path,
                               void (*callback)(const char *name,
                                                uint32_t size,
                                                bool is_dir,
                                                void *user),
                               void *user)
{
    fat32_list_dir_ctx((fat32_ctx_t *)vctx, path, callback, user);
}

static bool fat32_vfs_mount(block_device_t *dev, void **out_ctx)
{
    return fat32_mount(dev, (fat32_ctx_t **)out_ctx);
}

void fat32_register(void)
{
    static vfs_fs_type_t fs;

    fs.name = "fat32";
    fs.probe = fat32_probe;
    fs.mount = fat32_vfs_mount;
    fs.read_file = fat32_vfs_read_file;
    fs.list_dir = fat32_vfs_list_dir;
    vfs_register_fs(&fs);
}
