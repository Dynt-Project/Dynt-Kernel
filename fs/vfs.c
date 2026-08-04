#include "vfs.h"

#include "../mem/lib/memory.h"
#include "../mem/mm/kheap.h"

static vfs_fs_type_t *fs_types;
static uint32_t mount_used;
static vfs_mount_t *first_mount;
static vfs_mount_t *last_mount;

void vfs_init(void)
{
    fs_types = 0;
    mount_used = 0;
    first_mount = 0;
    last_mount = 0;
}

bool vfs_register_fs(vfs_fs_type_t *fs)
{
    if (!fs || !fs->name || !fs->probe)
        return false;

    fs->next = fs_types;
    fs_types = fs;
    return true;
}

static bool mount_device(block_device_t *dev, const vfs_fs_type_t *fs)
{
    vfs_mount_t *mount = (vfs_mount_t *)kheap_alloc(sizeof(vfs_mount_t), 16);

    if (!mount)
        return false;

    mount_used++;
    uint32_t index = mount_used;

    /* First mount goes at "/", others at "/diskNN" */
    if (index == 1)
    {
        k_strncpy(mount->path, "/", sizeof(mount->path));
    }
    else
    {
        k_strncpy(mount->path, "/disk", sizeof(mount->path));
        uint32_t pos = (uint32_t)k_strlen(mount->path);

        if (pos + 4 < sizeof(mount->path))
        {
            if (index >= 10)
                mount->path[pos++] = (char)('0' + (index / 10));
            mount->path[pos++] = (char)('0' + (index % 10));
            mount->path[pos] = 0;
        }
    }

    mount->fs = fs;
    mount->dev = dev;
    mount->fs_data = 0;
    mount->next = 0;

    /* Call the filesystem's mount function to get a context */
    if (fs->mount)
    {
        if (!fs->mount(dev, &mount->fs_data))
        {
            mount_used--;
            return false;
        }
    }

    if (!first_mount)
        first_mount = mount;
    else
        last_mount->next = mount;

    last_mount = mount;
    return true;
}

uint32_t vfs_mount_all(void)
{
    uint32_t mounted = 0;

    for (block_device_t *dev = block_device_first(); dev; dev = block_device_next(dev))
    {
        for (vfs_fs_type_t *fs = fs_types; fs; fs = fs->next)
        {
            if (fs->probe(dev))
            {
                if (mount_device(dev, fs))
                    mounted++;
                break;
            }
        }
    }

    return mounted;
}

uint32_t vfs_mount_count(void)
{
    return mount_used;
}

vfs_mount_t *vfs_mount_first(void)
{
    return first_mount;
}

vfs_mount_t *vfs_find_mount(const char *path)
{
    if (!path)
        return 0;

    for (vfs_mount_t *m = first_mount; m; m = m->next)
    {
        uint32_t plen = (uint32_t)k_strlen(m->path);

        if (k_strncmp(m->path, path, plen) == 0)
        {
            if (path[plen] == 0 || path[plen] == '/' || plen == 1)
                return m;
        }
    }

    return 0;
}

static const char *strip_mount_prefix(vfs_mount_t *m, const char *path)
{
    if (!m || !path)
        return path;

    uint32_t plen = (uint32_t)k_strlen(m->path);

    if (plen == 1)
    {
        if (path[0] == '/')
            return path + 1;
        return path;
    }

    if (k_strncmp(m->path, path, plen) == 0)
    {
        path += plen;
        if (path[0] == '/')
            path++;
    }

    return path;
}

int32_t vfs_read_file(const char *path, void *buffer, uint32_t buffer_size)
{
    vfs_mount_t *m = vfs_find_mount(path);

    if (!m || !m->fs || !m->fs->read_file || !buffer)
        return -1;

    const char *rel = strip_mount_prefix(m, path);

    return m->fs->read_file(m->fs_data, rel, buffer, buffer_size);
}

bool vfs_write_file(const char *path, const void *buffer, uint32_t size)
{
    vfs_mount_t *m = vfs_find_mount(path);

    if (!m || !m->fs || !m->fs->write_file || (size > 0 && !buffer))
        return false;

    const char *rel = strip_mount_prefix(m, path);

    return m->fs->write_file(m->fs_data, rel, buffer, size);
}

void vfs_list_dir(const char *path,
                  void (*callback)(const char *name,
                                   uint32_t size,
                                   bool is_dir,
                                   void *user),
                  void *user)
{
    vfs_mount_t *m = vfs_find_mount(path);

    if (!m || !m->fs || !m->fs->list_dir || !callback)
        return;

    const char *rel = strip_mount_prefix(m, path);

    m->fs->list_dir(m->fs_data, rel, callback, user);
}