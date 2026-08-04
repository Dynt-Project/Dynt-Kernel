#include "vfs.h"

#include "../mem/lib/memory.h"
#include "../mem/mm/kheap.h"
#include "../process/process.h"
#include "../init/debug.h"

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

int32_t vfs_stat(const char *path, uint64_t *size, bool *is_dir)
{
    vfs_mount_t *m = vfs_find_mount(path);

    if (!m || !m->fs || !m->fs->stat_file || !size || !is_dir)
        return -1;

    const char *rel = strip_mount_prefix(m, path);

    if (!m->fs->stat_file(m->fs_data, rel, size, is_dir))
        return -1;

    return 0;
}

/* ---- fd-based file I/O ---- */

int32_t vfs_open_fd(process_t *proc, const char *path, uint32_t flags)
{
    if (!proc || !path)
        return -1;

    uint64_t size = 0;
    bool is_dir = false;
    bool exists = vfs_stat(path, &size, &is_dir) == 0;

    if (exists && is_dir)
        return -1;

    if (!exists)
    {
        if (!(flags & VFS_O_CREAT))
            return -1;
        if (!vfs_write_file(path, 0, 0))
            return -1;
        size = 0;
    }
    else if ((flags & VFS_O_TRUNC) && (flags & (VFS_O_WRONLY | VFS_O_RDWR)))
    {
        if (!vfs_write_file(path, 0, 0))
            return -1;
        size = 0;
    }

    int32_t fd = -1;
    for (int i = 3; i < PROCESS_MAX_FDS; i++)
    {
        if (!proc->files[i].open)
        {
            fd = i;
            break;
        }
    }
    if (fd < 0)
        return -1;

    k_strncpy(proc->files[fd].path, path, sizeof(proc->files[fd].path));
    proc->files[fd].offset = (flags & VFS_O_APPEND) ? size : 0;
    proc->files[fd].open = true;
    return fd;
}

int64_t vfs_read_fd(process_t *proc, int32_t fd, void *buf, uint64_t len)
{
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS || !proc->files[fd].open)
        return -1;

    proc_file_t *f = &proc->files[fd];
    vfs_mount_t *m = vfs_find_mount(f->path);

    if (!m || !m->fs || !buf)
        return -1;

    const char *rel = strip_mount_prefix(m, f->path);

    uint64_t size = 0;
    bool is_dir = false;
    if (m->fs->stat_file && m->fs->stat_file(m->fs_data, rel, &size, &is_dir))
    {
        if (f->offset >= size)
            return 0;
        if (len > size - f->offset)
            len = size - f->offset;
    }

    int32_t n;
    if (m->fs->read_at)
        n = m->fs->read_at(m->fs_data, rel, (uint32_t)f->offset, buf,
                           (uint32_t)len);
    else if (m->fs->read_file)
    {
        if (f->offset != 0 || len > 0x7fffffff)
            return -1;
        n = m->fs->read_file(m->fs_data, rel, buf, (uint32_t)len);
    }
    else
        return -1;

    if (n > 0)
        f->offset += (uint64_t)n;
    return n;
}

int64_t vfs_write_fd(process_t *proc, int32_t fd, const void *buf,
                     uint64_t len)
{
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS || !proc->files[fd].open)
        return -1;

    proc_file_t *f = &proc->files[fd];
    vfs_mount_t *m = vfs_find_mount(f->path);

    if (!m || !m->fs || (len > 0 && !buf))
        return -1;

    const char *rel = strip_mount_prefix(m, f->path);

    if (f->offset > 0xffffffffULL)
        return -1;

    if (m->fs->write_at)
    {
        uint32_t new_size = 0;
        if (!m->fs->write_at(m->fs_data, rel, (uint32_t)f->offset, buf,
                             (uint32_t)len, &new_size))
            return -1;
        f->offset += len;
        return (int64_t)len;
    }

    if (f->offset != 0 || !m->fs->write_file)
        return -1;

    if (!m->fs->write_file(m->fs_data, rel, buf, (uint32_t)len))
        return -1;

    f->offset += len;
    return (int64_t)len;
}

int64_t vfs_seek_fd(process_t *proc, int32_t fd, int64_t off,
                    uint32_t whence)
{
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS || !proc->files[fd].open)
        return -1;

    proc_file_t *f = &proc->files[fd];
    uint64_t size = 0;
    bool is_dir = false;

    int64_t base = 0;
    if (whence == VFS_SEEK_CUR)
        base = (int64_t)f->offset;
    else if (whence == VFS_SEEK_END)
    {
        if (vfs_stat(f->path, &size, &is_dir) != 0)
            return -1;
        base = (int64_t)size;
    }
    else if (whence != VFS_SEEK_SET)
        return -1;

    if (base < 0 || base + off < 0)
        return -1;

    f->offset = (uint64_t)(base + off);
    return (int64_t)f->offset;
}

int32_t vfs_close_fd(process_t *proc, int32_t fd)
{
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS || !proc->files[fd].open)
        return -1;

    proc->files[fd].open = false;
    return 0;
}