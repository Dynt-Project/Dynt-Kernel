#include "fat32.h"

#include "vfs.h"
#include "../mem/lib/memory.h"
#include "../mem/mm/kheap.h"
#include "../init/debug.h"

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
    uint8_t *tmp = 0;
    uint32_t total = 0;
    uint32_t cluster = start_cluster;

    while (cluster_valid(cluster) && total < buffer_size)
    {
        uint32_t chunk = ctx->bytes_per_cluster;
        uint32_t left = buffer_size - total;

        if (chunk > left)
            chunk = left;

        uint32_t sectors = chunk / ctx->bytes_per_sector;

        if (sectors > 0 && (chunk % ctx->bytes_per_sector) == 0)
        {
            if (!block_read(ctx->dev, cluster_to_lba(ctx, cluster), sectors, dst + total))
                break;
        }
        else
        {
            if (!tmp)
                tmp = (uint8_t *)kheap_alloc(ctx->bytes_per_cluster, 16);

            if (!tmp ||
                !block_read(ctx->dev, cluster_to_lba(ctx, cluster),
                            ctx->sectors_per_cluster, tmp))
                break;

            k_memcpy(dst + total, tmp, chunk);
        }

        total += chunk;
        cluster = fat_read_entry(ctx, cluster);
    }

    if (tmp)
        kheap_free(tmp);

    return total;
}

uint32_t fat32_read_at(fat32_ctx_t *ctx, uint32_t start_cluster,
                       uint32_t offset, void *buffer, uint32_t buffer_size)
{
    if (!ctx || !buffer || buffer_size == 0)
        return 0;

    uint32_t bpc = ctx->bytes_per_cluster;
    uint32_t cluster = start_cluster;
    uint32_t cl_idx = 0;
    uint32_t start_cl_idx = offset / bpc;
    uint32_t in_cluster = offset % bpc;

    while (cl_idx < start_cl_idx && cluster_valid(cluster))
    {
        cluster = fat_read_entry(ctx, cluster);
        cl_idx++;
    }

    if (!cluster_valid(cluster))
        return 0;

    uint8_t *dst = (uint8_t *)buffer;
    uint8_t *tmp = (uint8_t *)kheap_alloc(bpc, 16);

    if (!tmp)
        return 0;

    uint32_t total = 0;

    while (total < buffer_size)
    {
        uint32_t left = bpc - in_cluster;
        uint32_t chunk = buffer_size - total;

        if (chunk > left)
            chunk = left;

        uint32_t sectors = chunk / ctx->bytes_per_sector;

        if (in_cluster == 0 && chunk == bpc)
        {
            if (!block_read(ctx->dev, cluster_to_lba(ctx, cluster),
                            ctx->sectors_per_cluster, dst + total))
                break;
        }
        else
        {
            if (!block_read(ctx->dev, cluster_to_lba(ctx, cluster),
                            ctx->sectors_per_cluster, tmp))
                break;

            k_memcpy(dst + total, tmp + in_cluster, chunk);
        }

        total += chunk;
        in_cluster += chunk;

        if (in_cluster == bpc)
        {
            in_cluster = 0;
            cluster = fat_read_entry(ctx, cluster);

            if (!cluster_valid(cluster))
                break;
        }
    }

    kheap_free(tmp);
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

static bool fat_write_entry(const fat32_ctx_t *ctx, uint32_t cluster, uint32_t value)
{
    uint32_t entry_lba = ctx->fat_start + (cluster * 4) / ctx->bytes_per_sector;
    uint32_t entry_off = (cluster * 4) % ctx->bytes_per_sector;

    if (!block_read(ctx->dev, entry_lba, 1, fat_sector))
        return false;

    uint32_t cur = k_le32(fat_sector + entry_off);
    uint32_t newval = (cur & 0xF0000000u) | (value & 0x0FFFFFFFu);

    fat_sector[entry_off + 0] = (uint8_t)(newval);
    fat_sector[entry_off + 1] = (uint8_t)(newval >> 8);
    fat_sector[entry_off + 2] = (uint8_t)(newval >> 16);
    fat_sector[entry_off + 3] = (uint8_t)(newval >> 24);

    return block_write(ctx->dev, entry_lba, 1, fat_sector);
}

static uint32_t fat32_find_free(const fat32_ctx_t *ctx)
{
    uint32_t total = ctx->fat_size * ctx->bytes_per_sector / 4;

    for (uint32_t cl = 2; cl < total; cl++)
    {
        if (fat_read_entry(ctx, cl) == 0)
            return cl;
    }

    return 0;
}

static uint32_t fat32_alloc_cluster(const fat32_ctx_t *ctx)
{
    uint32_t cl = fat32_find_free(ctx);

    if (cl == 0)
        return 0;

    if (!fat_write_entry(ctx, cl, FAT32_EOC))
        return 0;

    return cl;
}

static void fat32_free_chain(const fat32_ctx_t *ctx, uint32_t start)
{
    uint32_t cl = start;
    uint32_t guard = 0;

    while (cluster_valid(cl) && guard++ < 0x1000000u)
    {
        uint32_t next = fat_read_entry(ctx, cl);

        if (!fat_write_entry(ctx, cl, 0))
            break;

        if (next == cl || next >= FAT32_BAD)
            break;

        cl = next;
    }
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

typedef struct fat32_entry_loc
{
    fat32_dirent_t d;
    uint32_t cluster;
    uint32_t offset;
} fat32_entry_loc_t;

static bool locate_entry(fat32_ctx_t *ctx, uint32_t dir_cluster,
                         const char *name, fat32_entry_loc_t *out)
{
    if (!ctx || !name || !out)
        return false;

    uint8_t *buf = (uint8_t *)kheap_alloc(ctx->bytes_per_cluster, 16);

    if (!buf)
        return false;

    uint32_t cluster = dir_cluster;
    bool found = false;
    bool end = false;

    while (cluster_valid(cluster) && !end)
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
                end = true;
                break;
            }

            if (entry[0] == 0xE5)
                continue;

            if (entry_to_dirent(entry, &out->d) &&
                name_matches(out->d.name, name))
            {
                out->cluster = cluster;
                out->offset = i * 32;
                found = true;
                break;
            }
        }

        if (!end && !found)
            cluster = fat_read_entry(ctx, cluster);
    }

    kheap_free(buf);
    return found;
}

static bool dir_find_free(fat32_ctx_t *ctx, uint32_t dir_cluster,
                          uint32_t *out_cluster, uint32_t *out_offset)
{
    uint8_t *buf = (uint8_t *)kheap_alloc(ctx->bytes_per_cluster, 16);

    if (!buf)
        return false;

    uint32_t cluster = dir_cluster;
    uint32_t last_cluster = dir_cluster;
    bool found = false;

    while (cluster_valid(cluster))
    {
        if (!block_read(ctx->dev, cluster_to_lba(ctx, cluster),
                        ctx->sectors_per_cluster, buf))
            break;

        uint32_t count = ctx->bytes_per_cluster / 32;

        for (uint32_t i = 0; i < count; i++)
        {
            uint8_t b0 = buf[i * 32];

            if (b0 == 0x00 || b0 == 0xE5)
            {
                *out_cluster = cluster;
                *out_offset = i * 32;
                found = true;
                break;
            }
        }

        if (found)
            break;

        last_cluster = cluster;
        cluster = fat_read_entry(ctx, cluster);
    }

    kheap_free(buf);

    if (found)
        return true;

    uint32_t newcl = fat32_alloc_cluster(ctx);

    if (!newcl)
        return false;

    if (!fat_write_entry(ctx, last_cluster, newcl))
    {
        fat32_free_chain(ctx, newcl);
        return false;
    }

    uint8_t *zbuf = (uint8_t *)kheap_alloc(ctx->bytes_per_cluster, 16);

    if (!zbuf)
        return false;

    k_memset(zbuf, 0, ctx->bytes_per_cluster);
    bool ok = block_write(ctx->dev, cluster_to_lba(ctx, newcl),
                          ctx->sectors_per_cluster, zbuf);
    kheap_free(zbuf);

    if (!ok)
    {
        fat32_free_chain(ctx, newcl);
        return false;
    }

    *out_cluster = newcl;
    *out_offset = 0;
    return true;
}

static bool valid_short_char(uint8_t c)
{
    if (c >= 'A' && c <= 'Z')
        return true;
    if (c >= '0' && c <= '9')
        return true;
    if (c == '_' || c == '-' || c == '!' || c == '$' || c == '^')
        return true;
    return false;
}

static bool encode_short_name(const char *name, uint8_t out[11])
{
    const char *p = name;
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    uint32_t ext_start = (uint32_t)-1;

    for (uint32_t i = 0; p[i] != 0; i++)
    {
        if (p[i] == '.')
            ext_start = i;
    }

    if (ext_start == 0)
        return false;

    for (uint32_t i = 0; i < 11; i++)
        out[i] = ' ';

    uint32_t pos = 0;

    while (*p && pos < 8)
    {
        if (ext_start != (uint32_t)-1 && pos == ext_start)
            break;

        uint8_t c = (uint8_t)*p++;

        if (c >= 'a' && c <= 'z')
            c = (uint8_t)(c - 'a' + 'A');

        if (!valid_short_char(c))
            return false;

        out[pos++] = c;
        base_len++;
    }

    if (base_len == 0)
        return false;

    if (ext_start != (uint32_t)-1)
    {
        const char *e = name + ext_start + 1;

        for (uint32_t i = 0; i < 3 && *e; i++)
        {
            uint8_t c = (uint8_t)*e++;

            if (c >= 'a' && c <= 'z')
                c = (uint8_t)(c - 'a' + 'A');

            if (!valid_short_char(c))
                return false;

            out[8 + i] = c;
            ext_len++;
        }
    }

    (void)ext_len;
    return true;
}

static bool write_dir_entry_at(fat32_ctx_t *ctx, uint32_t cluster,
                               uint32_t offset, const char *name,
                               uint32_t first_cluster, uint32_t size)
{
    uint8_t *buf = (uint8_t *)kheap_alloc(ctx->bytes_per_cluster, 16);

    if (!buf)
        return false;

    if (!block_read(ctx->dev, cluster_to_lba(ctx, cluster),
                    ctx->sectors_per_cluster, buf))
    {
        kheap_free(buf);
        return false;
    }

    uint8_t *e = buf + offset;

    if (!encode_short_name(name, e))
    {
        kheap_free(buf);
        return false;
    }

    e[11] = 0x20;
    e[20] = (uint8_t)((first_cluster >> 16) & 0xFF);
    e[21] = (uint8_t)((first_cluster >> 24) & 0xFF);
    e[26] = (uint8_t)(first_cluster & 0xFF);
    e[27] = (uint8_t)((first_cluster >> 8) & 0xFF);
    e[28] = (uint8_t)(size & 0xFF);
    e[29] = (uint8_t)((size >> 8) & 0xFF);
    e[30] = (uint8_t)((size >> 16) & 0xFF);
    e[31] = (uint8_t)((size >> 24) & 0xFF);

    bool ok = block_write(ctx->dev, cluster_to_lba(ctx, cluster),
                          ctx->sectors_per_cluster, buf);
    kheap_free(buf);
    return ok;
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

bool fat32_write_file(fat32_ctx_t *ctx, const char *path,
                      const void *buffer, uint32_t size)
{
    if (!ctx || !path || (size > 0 && !buffer))
        return false;

    char components[8][FAT32_NAME_MAX];
    uint32_t depth = 0;
    const char *p = path;
    char comp[FAT32_NAME_MAX];

    while (split_component(&p, comp, sizeof(comp)))
    {
        if (depth >= 8)
            return false;

        k_strncpy(components[depth], comp, sizeof(components[depth]));
        depth++;
    }

    if (depth == 0)
        return false;

    uint32_t dir_cluster = ctx->root_cluster;

    for (uint32_t i = 0; i + 1 < depth; i++)
    {
        fat32_dirent_t e;

        if (!find_entry(ctx, dir_cluster, components[i], &e) || !e.is_dir)
            return false;

        dir_cluster = e.first_cluster;
    }

    fat32_entry_loc_t loc;
    bool exists = locate_entry(ctx, dir_cluster, components[depth - 1], &loc);

    uint8_t *cbuf = (uint8_t *)kheap_alloc(ctx->bytes_per_cluster, 16);

    if (!cbuf)
        return false;

    uint32_t first_cluster = 0;
    uint32_t prev = 0;
    uint32_t total = (size + ctx->bytes_per_cluster - 1) / ctx->bytes_per_cluster;
    bool ok = true;

    for (uint32_t i = 0; i < total; i++)
    {
        uint32_t cl = fat32_alloc_cluster(ctx);

        if (!cl)
        {
            ok = false;
            break;
        }

        if (!first_cluster)
        {
            first_cluster = cl;
        }
        else if (!fat_write_entry(ctx, prev, cl))
        {
            fat32_free_chain(ctx, first_cluster);
            ok = false;
            break;
        }

        uint32_t chunk = ctx->bytes_per_cluster;
        uint32_t left = size - i * ctx->bytes_per_cluster;

        if (chunk > left)
            chunk = left;

        k_memset(cbuf, 0, ctx->bytes_per_cluster);
        k_memcpy(cbuf, (const uint8_t *)buffer + i * ctx->bytes_per_cluster, chunk);

        if (!block_write(ctx->dev, cluster_to_lba(ctx, cl),
                         ctx->sectors_per_cluster, cbuf))
        {
            fat32_free_chain(ctx, first_cluster);
            ok = false;
            break;
        }

        prev = cl;
    }

    kheap_free(cbuf);

    if (!ok)
        return false;

    if (exists)
    {
        if (loc.d.first_cluster >= 2)
            fat32_free_chain(ctx, loc.d.first_cluster);

        return write_dir_entry_at(ctx, loc.cluster, loc.offset,
                                  components[depth - 1], first_cluster, size);
    }

    uint32_t slot_cluster;
    uint32_t slot_offset;

    if (!dir_find_free(ctx, dir_cluster, &slot_cluster, &slot_offset))
        return false;

    return write_dir_entry_at(ctx, slot_cluster, slot_offset,
                              components[depth - 1], first_cluster, size);
}

// offset-based write into an existing file; extends the cluster chain as
// needed. *first_cluster is in/out: a zero-size file has no cluster, so the
// first allocated cluster is returned through it.
uint32_t fat32_write(fat32_ctx_t *ctx, uint32_t *first_cluster, uint32_t offset,
                     const void *buffer, uint32_t size)
{
    if (!ctx || !first_cluster || !buffer || size == 0)
        return 0;

    uint32_t bpc = ctx->bytes_per_cluster;
    const uint8_t *src = (const uint8_t *)buffer;
    uint8_t *cbuf = (uint8_t *)kheap_alloc(bpc, 16);

    if (!cbuf)
        return 0;

    uint32_t in_cluster = offset % bpc;
    uint32_t cluster = *first_cluster;
    uint32_t prev = *first_cluster;
    uint32_t cl_idx = 0;
    uint32_t start_cl_idx = offset / bpc;

    while (cl_idx < start_cl_idx && cluster_valid(cluster))
    {
        prev = cluster;
        cluster = fat_read_entry(ctx, cluster);
        cl_idx++;
    }

    uint32_t done = 0;

    while (done < size)
    {
        if (!cluster_valid(cluster))
        {
            uint32_t newcl = fat32_alloc_cluster(ctx);

            if (!newcl)
                break;

            if (!*first_cluster)
            {
                *first_cluster = newcl;
            }
            else if (!fat_write_entry(ctx, prev, newcl))
            {
                fat32_free_chain(ctx, newcl);
                break;
            }

            cluster = newcl;
        }

        uint32_t left = bpc - in_cluster;
        uint32_t chunk = size - done;

        if (chunk > left)
            chunk = left;

        if (in_cluster == 0 && chunk == bpc)
        {
            if (!block_write(ctx->dev, cluster_to_lba(ctx, cluster),
                             ctx->sectors_per_cluster, src + done))
                break;
        }
        else
        {
            if (!block_read(ctx->dev, cluster_to_lba(ctx, cluster),
                            ctx->sectors_per_cluster, cbuf))
                break;

            k_memcpy(cbuf + in_cluster, src + done, chunk);

            if (!block_write(ctx->dev, cluster_to_lba(ctx, cluster),
                             ctx->sectors_per_cluster, cbuf))
                break;
        }

        done += chunk;
        in_cluster += chunk;

        if (in_cluster == bpc)
        {
            in_cluster = 0;
            prev = cluster;
            cluster = fat_read_entry(ctx, cluster);
        }
    }

    kheap_free(cbuf);
    return done;
}

// updates size + first cluster of an existing file's directory entry
bool fat32_update_size(fat32_ctx_t *ctx, const char *path,
                       uint32_t first_cluster, uint32_t size)
{
    if (!ctx || !path)
        return false;

    char components[8][FAT32_NAME_MAX];
    uint32_t depth = 0;
    const char *p = path;
    char comp[FAT32_NAME_MAX];

    while (split_component(&p, comp, sizeof(comp)))
    {
        if (depth >= 8)
            return false;

        k_strncpy(components[depth], comp, sizeof(components[depth]));
        depth++;
    }

    if (depth == 0)
        return false;

    uint32_t dir_cluster = ctx->root_cluster;

    for (uint32_t i = 0; i + 1 < depth; i++)
    {
        fat32_dirent_t e;

        if (!find_entry(ctx, dir_cluster, components[i], &e) || !e.is_dir)
            return false;

        dir_cluster = e.first_cluster;
    }

    fat32_entry_loc_t loc;

    if (!locate_entry(ctx, dir_cluster, components[depth - 1], &loc))
        return false;

    return write_dir_entry_at(ctx, loc.cluster, loc.offset,
                              components[depth - 1], first_cluster, size);
}

bool fat32_stat(fat32_ctx_t *ctx, const char *path, uint64_t *size, bool *is_dir)
{
    fat32_dirent_t entry;

    if (!ctx || !path || !fat32_open(ctx, path, &entry))
        return false;

    if (size)
        *size = entry.size;

    if (is_dir)
        *is_dir = entry.is_dir;

    return true;
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

static int32_t fat32_vfs_read_at(void *vctx, const char *path,
                                 uint32_t offset, void *buffer,
                                 uint32_t buffer_size)
{
    fat32_ctx_t *ctx = (fat32_ctx_t *)vctx;

    if (!ctx || !buffer || !path)
        return -1;

    fat32_dirent_t entry;

    if (!fat32_open(ctx, path, &entry) || entry.is_dir)
        return -1;

    if (offset >= entry.size)
        return 0;

    if (buffer_size > entry.size - offset)
        buffer_size = entry.size - offset;

    return (int32_t)fat32_read_at(ctx, entry.first_cluster, offset, buffer,
                                  buffer_size);
}

static bool fat32_vfs_write_at(void *vctx, const char *path,
                               uint32_t offset, const void *buffer,
                               uint32_t size, uint32_t *out_new_size)
{
    fat32_ctx_t *ctx = (fat32_ctx_t *)vctx;

    if (!ctx || !path || (size > 0 && !buffer))
        return false;

    fat32_dirent_t entry;

    if (!fat32_open(ctx, path, &entry) || entry.is_dir)
        return false;

    uint32_t first_cluster = entry.first_cluster;

    uint32_t done = fat32_write(ctx, &first_cluster, offset, buffer, size);

    if (done != size)
        return false;

    uint32_t new_size = entry.size;

    if (offset + size > new_size)
        new_size = offset + size;

    if (!fat32_update_size(ctx, path, first_cluster, new_size))
        return false;

    if (out_new_size)
        *out_new_size = new_size;

    return true;
}

static bool fat32_vfs_stat_file(void *vctx, const char *path, uint64_t *size,
                                bool *is_dir)
{
    return fat32_stat((fat32_ctx_t *)vctx, path, size, is_dir);
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

    if (entry.size < buffer_size)
        buffer_size = entry.size;

    return (int32_t)fat32_read(ctx, entry.first_cluster, buffer, buffer_size);
}

static bool fat32_vfs_write_file(void *vctx, const char *path,
                                 const void *buffer, uint32_t size)
{
    return fat32_write_file((fat32_ctx_t *)vctx, path, buffer, size);
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
    fs.write_file = fat32_vfs_write_file;
    fs.stat_file = fat32_vfs_stat_file;
    fs.read_at = fat32_vfs_read_at;
    fs.write_at = fat32_vfs_write_at;
    fs.list_dir = fat32_vfs_list_dir;
    vfs_register_fs(&fs);
}
