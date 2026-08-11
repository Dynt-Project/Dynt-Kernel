#include "fat32.h"

#include "vfs.h"
#include "../mem/lib/memory.h"
#include "../mem/mm/kheap.h"

#define FAT32_EOC 0x0FFFFFF8
#define FAT32_BAD 0x0FFFFFF7

#define FAT32_DIR_ATTR_DIRECTORY 0x10

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

/* ---- long file name (LFN) support ----
 *
 * An LFN name is stored in a run of directory entries with attribute
 * 0x0F that directly precede its 8.3 entry. Every LFN entry holds 13
 * UTF-16 chars; the parts are stored backwards (the last 13 chars come
 * first in the directory) and tied to the 8.3 entry by a checksum. */

#define FAT32_DIR_ATTR_LFN 0x0F
#define LFN_MAX_PARTS 20    /* 255 chars / 13 */

// the 13 chars of one LFN part, decoded from UTF-16 (ASCII subset only)
static void lfn_parse_part(const uint8_t *e, char *out)
{
    int idx = 0;

    for (int i = 0; i < 13; i++)
    {
        int off;

        if (i < 5)
            off = 1 + 2 * i;
        else if (i < 11)
            off = 14 + 2 * (i - 5);
        else
            off = 28 + 2 * (i - 11);

        uint16_t ch = (uint16_t)(e[off] | (uint16_t)e[off + 1] << 8);

        if (ch == 0x0000 || ch == 0xFFFF)
        {
            out[idx] = 0;
            return;
        }

        out[idx++] = (e[off + 1] == 0) ? (char)e[off] : '?';
    }

    out[idx] = 0;
}

typedef struct
{
    uint8_t seq[LFN_MAX_PARTS];
    char    chars[LFN_MAX_PARTS][14];
    uint32_t count;
} lfn_acc_t;

static void lfn_reset(lfn_acc_t *a)
{
    a->count = 0;
}

static void lfn_add_part(lfn_acc_t *a, const uint8_t *e)
{
    if (a->count >= LFN_MAX_PARTS)
    {
        lfn_reset(a);
        return;
    }

    a->seq[a->count] = e[0] & 0x3F;
    lfn_parse_part(e, a->chars[a->count]);
    a->count++;
}

// rebuilds the name: parts appear in the directory with the highest
// sequence number first, so they are joined in ascending seq order
static void lfn_assemble(const lfn_acc_t *a, char *out, uint32_t out_size)
{
    uint32_t pos = 0;

    for (uint32_t s = 1; s <= a->count; s++)
    {
        for (uint32_t p = 0; p < a->count; p++)
        {
            if (a->seq[p] == s)
            {
                for (const char *c = a->chars[p]; *c && pos + 1 < out_size; c++)
                    out[pos++] = *c;
                break;
            }
        }
    }

    out[pos] = 0;
}

// fills a fat32_dirent_t from the raw 8.3 entry + the (long) name
static void make_dirent(const uint8_t *entry, const char *name,
                        fat32_dirent_t *out)
{
    k_memset(out, 0, sizeof(*out));
    k_strncpy(out->name, name, sizeof(out->name));
    out->size = k_le32(entry + 28);
    out->first_cluster = (uint32_t)k_le16(entry + 20) << 16 | k_le16(entry + 26);
    out->is_dir = (entry[11] & FAT32_DIR_ATTR_DIRECTORY) != 0;

    if (out->is_dir)
        out->size = 0;
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

// walks a directory cluster chain and visits every non-deleted 8.3 entry
// with its long (or short) name, cluster and offset. the callback returns
// true to stop the scan early.
static bool read_directory(fat32_ctx_t *ctx, uint32_t dir_cluster,
                           bool (*visit)(const fat32_ctx_t *ctx,
                                         uint32_t cluster, uint32_t offset,
                                         const uint8_t *entry,
                                         const char *name,
                                         void *user),
                           void *user)
{
    uint8_t *buf = (uint8_t *)kheap_alloc(ctx->bytes_per_cluster, 16);

    if (!buf)
        return false;

    uint32_t cluster = dir_cluster;
    bool found = false;
    bool end = false;
    lfn_acc_t acc;
    lfn_reset(&acc);

    while (cluster_valid(cluster) && !end && !found)
    {
        if (!block_read(ctx->dev, cluster_to_lba(ctx, cluster),
                        ctx->sectors_per_cluster, buf))
            break;

        uint32_t count = ctx->bytes_per_cluster / 32;

        for (uint32_t i = 0; i < count; i++)
        {
            const uint8_t *entry = buf + i * 32;
            uint8_t b0 = entry[0];

            if (b0 == 0x00)
            {
                end = true;
                break;
            }

            if (b0 == 0xE5)
            {
                lfn_reset(&acc);
                continue;
            }

            uint8_t attr = entry[11];

            if ((attr & FAT32_DIR_ATTR_LFN) == FAT32_DIR_ATTR_LFN)
            {
                lfn_add_part(&acc, entry);
                continue;
            }

            char name[FAT32_NAME_MAX];

            if (acc.count > 0)
                lfn_assemble(&acc, name, sizeof(name));
            else
                build_short_name(entry, name, sizeof(name));

            lfn_reset(&acc);

            if (visit(ctx, cluster, i * 32, entry, name, user))
            {
                found = true;
                break;
            }
        }
    }

    kheap_free(buf);
    return found;
}

typedef struct open_search
{
    const char *name;
    fat32_dirent_t *out;
    bool found;
} open_search_t;

static bool open_visit(const fat32_ctx_t *ctx, uint32_t cluster, uint32_t offset,
                       const uint8_t *entry, const char *name, void *user)
{
    (void)ctx;
    (void)cluster;
    (void)offset;
    open_search_t *s = (open_search_t *)user;

    if (s->found)
        return true;

    if (name_matches(name, s->name))
    {
        make_dirent(entry, name, s->out);
        s->found = true;
        return true;
    }

    return false;
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

typedef struct locate_search
{
    const char *name;
    fat32_entry_loc_t *out;
    bool found;
} locate_search_t;

static bool locate_visit(const fat32_ctx_t *ctx, uint32_t cluster,
                         uint32_t offset, const uint8_t *entry,
                         const char *name, void *user)
{
    (void)ctx;
    locate_search_t *s = (locate_search_t *)user;

    if (s->found)
        return true;

    if (name_matches(name, s->name))
    {
        make_dirent(entry, name, &s->out->d);
        s->out->cluster = cluster;
        s->out->offset = offset;
        s->found = true;
        return true;
    }

    return false;
}

static bool locate_entry(fat32_ctx_t *ctx, uint32_t dir_cluster,
                         const char *name, fat32_entry_loc_t *out)
{
    locate_search_t search;
    k_memset(&search, 0, sizeof(search));
    search.name = name;
    search.out = out;

    read_directory(ctx, dir_cluster, locate_visit, &search);
    return search.found;
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

static bool dir_find_free_slots(fat32_ctx_t *ctx, uint32_t dir_cluster,
                                uint32_t slots,
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
        uint32_t run = 0;

        for (uint32_t i = 0; i < count; i++)
        {
            uint8_t b0 = buf[i * 32];

            if (b0 == 0x00 || b0 == 0xE5)
            {
                run++;

                if (run == slots)
                {
                    *out_cluster = cluster;
                    *out_offset = (i - (slots - 1)) * 32;
                    found = true;
                    break;
                }
            }
            else
            {
                run = 0;
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
                               uint32_t first_cluster, uint32_t size,
                               bool is_dir)
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

    e[11] = is_dir ? FAT32_DIR_ATTR_DIRECTORY : 0x20;
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

/* ---- LFN creation ----
 *
 * A long name is stored as a chain of 13-char parts directly above its
 * 8.3 entry, the last part first, each part tied to the entry by the
 * checksum below. */

static uint8_t lfn_checksum(const uint8_t short11[11])
{
    uint8_t sum = 0;

    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1)) + short11[i];

    return sum;
}

// number of LFN parts needed for a name (0 = the short name is enough)
static uint32_t lfn_parts_for(const char *name, uint8_t short11[11])
{
    if (!encode_short_name(name, short11))
        return 0;

    // Windows writes an LFN whenever the name does not already match its
    // own uppercase 8.3 form: any lowercase char or any char that the
    // short name would lose (spaces, > 8.3 length, ...)
    bool short_is_exact = true;

    for (uint32_t i = 0; name[i] != 0; i++)
    {
        char c = name[i];

        if (c == '.')
            continue;

        if (c >= 'a' && c <= 'z')
        {
            short_is_exact = false;
            break;
        }

        if (c == ' ' || !valid_short_char((uint8_t)c))
        {
            short_is_exact = false;
            break;
        }
    }

    if (short_is_exact)
        return 0;

    uint32_t len = 0;

    while (name[len] != 0)
        len++;

    return (len + 12) / 13;
}

// writes one LFN part entry: seq is 1..parts, set_first marks the first
// (highest) part which carries the 0x40 bit
static void lfn_fill_part(uint8_t e[32], const char *name, uint32_t char_start,
                          uint8_t seq, bool set_first, uint8_t checksum)
{
    k_memset(e, 0xFF, 32);

    e[0] = set_first ? (uint8_t)(seq | 0x40) : seq;
    e[11] = FAT32_DIR_ATTR_LFN;
    e[13] = checksum;
    e[26] = 0;
    e[27] = 0;

    // positions of the 13 chars inside the 32-byte entry
    static const uint8_t char_offsets[13] = { 1, 3, 5, 7, 9, 14, 16, 18,
                                              20, 22, 24, 28, 30 };

    for (int i = 0; i < 13; i++)
    {
        char c = name[char_start + i];

        if (c == 0)
        {
            e[char_offsets[i]] = 0;
            e[char_offsets[i] + 1] = 0;
        }
        else
        {
            e[char_offsets[i]] = (uint8_t)c;
            e[char_offsets[i] + 1] = 0;
        }
    }
}

// creates the LFN chain + 8.3 entry at offset (the 8.3 slot) in a
// directory. the (n) LFN slots must lie directly above it, free.
static bool create_dir_entries(fat32_ctx_t *ctx, uint32_t cluster,
                               uint32_t offset, const char *name,
                               uint32_t first_cluster, uint32_t size,
                               bool is_dir)
{
    uint8_t short11[11];
    uint32_t parts = lfn_parts_for(name, short11);

    if (parts == 0)
        return write_dir_entry_at(ctx, cluster, offset, name,
                                  first_cluster, size, is_dir);

    uint8_t *buf = (uint8_t *)kheap_alloc(ctx->bytes_per_cluster, 16);

    if (!buf)
        return false;

    if (!block_read(ctx->dev, cluster_to_lba(ctx, cluster),
                    ctx->sectors_per_cluster, buf))
    {
        kheap_free(buf);
        return false;
    }

    uint8_t checksum = lfn_checksum(short11);
    uint32_t len = 0;

    while (name[len] != 0)
        len++;

    // parts are stored from the highest sequence number down to 1,
    // each holding the last 13 chars of the name in order
    for (uint32_t p = 0; p < parts; p++)
    {
        uint32_t char_start = len - (p + 1) * 13;
        int32_t slot = (int32_t)offset - (int32_t)(p + 1) * 32;

        if (slot < 0)
        {
            kheap_free(buf);
            return false;
        }

        lfn_fill_part(buf + slot, name, char_start,
                      (uint8_t)(parts - p), p == 0, checksum);
    }

    uint8_t *e = buf + offset;
    k_memcpy(e, short11, 11);
    e[11] = is_dir ? FAT32_DIR_ATTR_DIRECTORY : 0x20;
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
                                  components[depth - 1], first_cluster,
                                  size, false);
    }

    uint8_t short11[11];
    uint32_t parts = lfn_parts_for(components[depth - 1], short11);
    uint32_t slot_cluster;
    uint32_t slot_offset;

    // LFN parts live directly above the 8.3 slot, so the whole run of
    // parts + the 8.3 entry must be free as one contiguous block
    if (!dir_find_free_slots(ctx, dir_cluster, parts + 1,
                             &slot_cluster, &slot_offset))
        return false;

    return create_dir_entries(ctx, slot_cluster, slot_offset + parts * 32,
                              components[depth - 1], first_cluster, size,
                              false);
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
                              components[depth - 1], first_cluster,
                              size, false);
}

/* ---- mkdir / remove ---- */

// errno values returned as negative numbers
#define FAT32_ERRNO_ENOENT 2
#define FAT32_ERRNO_EEXIST 17
#define FAT32_ERRNO_EISDIR 21
#define FAT32_ERRNO_EINVAL 22
#define FAT32_ERRNO_ENOTEMPTY 39

static bool valid_component_name(const char *name)
{
    if (!name || name[0] == 0)
        return false;

    if (name[0] == '.')
    {
        if (name[1] == 0)
            return false;
        if (name[1] == '.' && name[2] == 0)
            return false;
    }

    for (uint32_t i = 0; name[i] != 0; i++)
    {
        char c = name[i];

        if (c < 0x20 || c == 0x7F)
            return false;

        switch (c)
        {
            case '*': case '?': case '<': case '>':
            case '|': case '"': case ':': case '\\':
                return false;
            default:
                break;
        }
    }

    return true;
}

// writes the "." (self) and ".." (parent) entries a new directory needs
static bool write_dot_entries(fat32_ctx_t *ctx, uint32_t dir_cluster,
                              uint32_t parent_cluster)
{
    uint8_t *buf = (uint8_t *)kheap_alloc(ctx->bytes_per_cluster, 16);

    if (!buf)
        return false;

    k_memset(buf, 0, ctx->bytes_per_cluster);

    uint8_t *self = buf;
    self[0] = '.';
    self[11] = FAT32_DIR_ATTR_DIRECTORY;
    self[20] = (uint8_t)((dir_cluster >> 16) & 0xFF);
    self[21] = (uint8_t)((dir_cluster >> 24) & 0xFF);
    self[26] = (uint8_t)(dir_cluster & 0xFF);
    self[27] = (uint8_t)((dir_cluster >> 8) & 0xFF);

    uint8_t *parent = buf + 32;
    parent[0] = '.';
    parent[1] = '.';
    parent[11] = FAT32_DIR_ATTR_DIRECTORY;
    parent[20] = (uint8_t)((parent_cluster >> 16) & 0xFF);
    parent[21] = (uint8_t)((parent_cluster >> 24) & 0xFF);
    parent[26] = (uint8_t)(parent_cluster & 0xFF);
    parent[27] = (uint8_t)((parent_cluster >> 8) & 0xFF);

    bool ok = block_write(ctx->dev, cluster_to_lba(ctx, dir_cluster),
                          ctx->sectors_per_cluster, buf);
    kheap_free(buf);
    return ok;
}

int32_t fat32_mkdir(fat32_ctx_t *ctx, const char *path)
{
    if (!ctx || !path)
        return -FAT32_ERRNO_EINVAL;

    char components[8][FAT32_NAME_MAX];
    uint32_t depth = 0;
    const char *p = path;
    char comp[FAT32_NAME_MAX];

    while (split_component(&p, comp, sizeof(comp)))
    {
        if (depth >= 8)
            return -FAT32_ERRNO_EINVAL;

        if (!valid_component_name(comp))
            return -FAT32_ERRNO_EINVAL;

        k_strncpy(components[depth], comp, sizeof(components[depth]));
        depth++;
    }

    if (depth == 0)
        return -FAT32_ERRNO_EINVAL;

    uint32_t dir_cluster = ctx->root_cluster;

    for (uint32_t i = 0; i + 1 < depth; i++)
    {
        fat32_dirent_t e;

        if (!find_entry(ctx, dir_cluster, components[i], &e))
            return -FAT32_ERRNO_ENOENT;

        if (!e.is_dir)
            return -FAT32_ERRNO_EISDIR;

        dir_cluster = e.first_cluster;
    }

    fat32_dirent_t existing;

    if (find_entry(ctx, dir_cluster, components[depth - 1], &existing))
        return -FAT32_ERRNO_EEXIST;

    uint32_t new_cluster = fat32_alloc_cluster(ctx);

    if (!new_cluster)
        return -FAT32_ERRNO_ENOTEMPTY;

    if (!write_dot_entries(ctx, new_cluster, dir_cluster))
    {
        fat32_free_chain(ctx, new_cluster);
        return -FAT32_ERRNO_EINVAL;
    }

    uint8_t short11[11];
    uint32_t parts = lfn_parts_for(components[depth - 1], short11);
    uint32_t slot_cluster;
    uint32_t slot_offset;

    if (!dir_find_free_slots(ctx, dir_cluster, parts + 1,
                             &slot_cluster, &slot_offset))
    {
        fat32_free_chain(ctx, new_cluster);
        return -FAT32_ERRNO_ENOTEMPTY;
    }

    if (!create_dir_entries(ctx, slot_cluster, slot_offset + parts * 32,
                            components[depth - 1], new_cluster, 0, true))
    {
        fat32_free_chain(ctx, new_cluster);
        return -FAT32_ERRNO_EINVAL;
    }

    return 0;
}

// marks an 8.3 entry and its preceding LFN parts as deleted
static bool mark_entry_deleted(fat32_ctx_t *ctx, uint32_t cluster,
                               uint32_t offset)
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

    if (e[0] == 0x00 || e[0] == 0xE5)
    {
        kheap_free(buf);
        return true;
    }

    e[0] = 0xE5;

    // LFN parts sit directly above the 8.3 entry, last part first
    for (int32_t off = (int32_t)offset - 32; off >= 0; off -= 32)
    {
        uint8_t *p = buf + off;

        if (p[11] == FAT32_DIR_ATTR_LFN)
            p[0] = 0xE5;
        else
            break;
    }

    bool ok = block_write(ctx->dev, cluster_to_lba(ctx, cluster),
                          ctx->sectors_per_cluster, buf);
    kheap_free(buf);
    return ok;
}

typedef struct remove_child
{
    fat32_dirent_t d;
    uint32_t cluster;
    uint32_t offset;
} remove_child_t;

#define REMOVE_MAX_CHILDREN 512

typedef struct remove_collect
{
    remove_child_t *items;
    uint32_t count;
    uint32_t capacity;
} remove_collect_t;

static bool remove_collect_visit(const fat32_ctx_t *ctx, uint32_t cluster,
                                 uint32_t offset, const uint8_t *entry,
                                 const char *name, void *user)
{
    (void)ctx;
    remove_collect_t *c = (remove_collect_t *)user;

    if (c->count >= c->capacity)
        return true;  // stop, dir is too full to remove safely

    make_dirent(entry, name, &c->items[c->count].d);
    c->items[c->count].cluster = cluster;
    c->items[c->count].offset = offset;
    c->count++;
    return false;
}

static int32_t remove_recursive(fat32_ctx_t *ctx, uint32_t dir_cluster,
                                uint32_t depth)
{
    if (depth > 16)
        return -FAT32_ERRNO_ENOTEMPTY;

    remove_child_t items[REMOVE_MAX_CHILDREN];
    remove_collect_t collect;

    collect.items = items;
    collect.count = 0;
    collect.capacity = REMOVE_MAX_CHILDREN;

    read_directory(ctx, dir_cluster, remove_collect_visit, &collect);

    for (uint32_t i = 0; i < collect.count; i++)
    {
        remove_child_t *ch = &collect.items[i];

        if (ch->d.is_dir)
        {
            int32_t rc = remove_recursive(ctx, ch->d.first_cluster, depth + 1);

            if (rc != 0)
                return rc;
        }
        else if (ch->d.first_cluster >= 2)
        {
            fat32_free_chain(ctx, ch->d.first_cluster);
        }

        mark_entry_deleted(ctx, ch->cluster, ch->offset);
    }

    if (dir_cluster >= 2)
        fat32_free_chain(ctx, dir_cluster);

    return 0;
}

int32_t fat32_remove(fat32_ctx_t *ctx, const char *path)
{
    if (!ctx || !path)
        return -FAT32_ERRNO_EINVAL;

    char components[8][FAT32_NAME_MAX];
    uint32_t depth = 0;
    const char *p = path;
    char comp[FAT32_NAME_MAX];

    while (split_component(&p, comp, sizeof(comp)))
    {
        if (depth >= 8)
            return -FAT32_ERRNO_EINVAL;

        k_strncpy(components[depth], comp, sizeof(components[depth]));
        depth++;
    }

    if (depth == 0)
        return -FAT32_ERRNO_EINVAL;

    uint32_t dir_cluster = ctx->root_cluster;

    for (uint32_t i = 0; i + 1 < depth; i++)
    {
        fat32_dirent_t e;

        if (!find_entry(ctx, dir_cluster, components[i], &e))
            return -FAT32_ERRNO_ENOENT;

        if (!e.is_dir)
            return -FAT32_ERRNO_EISDIR;

        dir_cluster = e.first_cluster;
    }

    fat32_entry_loc_t loc;

    if (!locate_entry(ctx, dir_cluster, components[depth - 1], &loc))
        return -FAT32_ERRNO_ENOENT;

    if (loc.d.is_dir)
    {
        int32_t rc = remove_recursive(ctx, loc.d.first_cluster, 0);

        if (rc != 0)
            return rc;
    }
    else if (loc.d.first_cluster >= 2)
    {
        fat32_free_chain(ctx, loc.d.first_cluster);
    }

    mark_entry_deleted(ctx, loc.cluster, loc.offset);
    return 0;
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

static bool list_visit(const fat32_ctx_t *ctx, uint32_t cluster, uint32_t offset,
                       const uint8_t *entry, const char *name, void *user)
{
    (void)ctx;
    (void)cluster;
    (void)offset;
    list_search_t *s = (list_search_t *)user;
    fat32_dirent_t d;

    make_dirent(entry, name, &d);
    s->callback(d.name, d.size, d.is_dir, s->user);
    return false;
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

static int32_t fat32_vfs_mkdir(void *vctx, const char *path)
{
    return fat32_mkdir((fat32_ctx_t *)vctx, path);
}

static int32_t fat32_vfs_remove(void *vctx, const char *path)
{
    return fat32_remove((fat32_ctx_t *)vctx, path);
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
    fs.mkdir = fat32_vfs_mkdir;
    fs.remove = fat32_vfs_remove;
    vfs_register_fs(&fs);
}
