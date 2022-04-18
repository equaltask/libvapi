#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/sendfile.h>
#include <sys/resource.h>
#include <errno.h>
#include <dirent.h>
#include <zstd.h>

#include <libvapi/vfs.h>
#include <libvapi/vmutex.h>
#include "vlog_vapi.h"

struct zstd_ctx {
    ZSTD_CCtx *cctx;
    size_t  buffInSize_c;
    size_t  buffOutSize_c;
    void   *buffIn_c;
    void   *buffOut_c;
    ZSTD_inBuffer input_c;
    ZSTD_outBuffer output_c;

    ZSTD_DCtx *dctx;
    size_t  buffInSize_d;
    size_t  buffOutSize_d;
    void   *buffIn_d;
    void   *buffOut_d;
    ZSTD_inBuffer input_d;
    ZSTD_outBuffer output_d;
    size_t  readPos_d;
    ssize_t remaining_d;
};
struct ycfs_handle {
    int fd;
    int vfs_mode;
    vthread_mutex_t lock;
    struct zstd_ctx zstd;
};

typedef struct ycfs_handle *ycfs_handle_t;
static void **vfs_private_data = NULL;
static struct rlimit rlim = {0, 0};

static ssize_t vfs_read_non_compressed(int fd, void *buffer, size_t nbytes);
static ssize_t vfs_write_non_compressed(int fd, const void *buffer, size_t nbytes);

static int vfs_private_data_init()
{
    if (vfs_private_data == NULL) {
        if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
            return -1;

        vapi_debug("[vfs]: Alloc private data for %ld num of fd", rlim.rlim_cur);
        vfs_private_data = vmem_calloc(vmem_alloc_default(), rlim.rlim_cur * sizeof(void *));
        if (!vfs_private_data)
            return -1;
    }

    return 0;
}

static void vfs_set_private_data(int fd, void *data)
{
    if (vfs_private_data) {
        if ((fd < 0) || ((unsigned)fd >= rlim.rlim_cur)) {
            vapi_info("vfs: invalid fd: %d, out of range [%d, %ld]\n", fd, 0, rlim.rlim_cur);
            return;
        }
        vfs_private_data[fd] = data;
    }
}

static void *vfs_get_private_data(int fd)
{
    if (vfs_private_data) {
        if ((fd < 0) || ((unsigned)fd >= rlim.rlim_cur)) {
            vapi_info("vfs: invalid fd: %d, out of range [%d, %ld]\n", fd, 0, rlim.rlim_cur);
            return NULL;
        }
        return vfs_private_data[fd];
    } else
        return NULL;
}

static void zstd_free(ycfs_handle_t handle)
{
    if (!handle) {
        return;
    }

    if (handle->zstd.cctx) {
        ZSTD_freeCCtx(handle->zstd.cctx);
    }
    if (handle->zstd.buffIn_c) {
        vmem_free(vmem_alloc_default(), handle->zstd.buffIn_c);
    }
    if (handle->zstd.buffOut_c) {
        vmem_free(vmem_alloc_default(), handle->zstd.buffOut_c);
    }
    if (handle->zstd.dctx) {
        ZSTD_freeDCtx(handle->zstd.dctx);
    }
    if (handle->zstd.buffIn_d) {
        vmem_free(vmem_alloc_default(), handle->zstd.buffIn_d);
    }
    if (handle->zstd.buffOut_d) {
        vmem_free(vmem_alloc_default(), handle->zstd.buffOut_d);
    }

    vmutex_delete(&handle->lock);
    vmem_free(vmem_alloc_default(), handle);

}

static ycfs_handle_t zstd_init(int clevel)
{
    size_t ret;

    ycfs_handle_t handle = vmem_calloc(vmem_alloc_default(), sizeof(struct ycfs_handle));
    if (!handle) {
        return NULL;
    }

    if (vmutex_create(&handle->lock)) {
        goto error;
    }

    handle->zstd.buffInSize_c = ZSTD_CStreamInSize();
    handle->zstd.buffOutSize_c = ZSTD_CStreamOutSize();
    handle->zstd.buffIn_c = vmem_calloc(vmem_alloc_default(), handle->zstd.buffInSize_c);
    if (!handle->zstd.buffIn_c) {
        goto error;
    }
    handle->zstd.buffOut_c = vmem_calloc(vmem_alloc_default(), handle->zstd.buffOutSize_c);
    if (!handle->zstd.buffOut_c) {
        goto error;
    }

    handle->zstd.output_c.dst = handle->zstd.buffOut_c;
    handle->zstd.output_c.size = handle->zstd.buffOutSize_c;

    handle->zstd.cctx = ZSTD_createCCtx();
    if (!handle->zstd.cctx) {
        vapi_info("zstd_init failed: could not create zstd cctx\n");
        goto error;
    }

    handle->zstd.buffInSize_d = ZSTD_DStreamInSize();
    handle->zstd.buffOutSize_d = ZSTD_DStreamOutSize();
    handle->zstd.buffIn_d = vmem_calloc(vmem_alloc_default(), handle->zstd.buffInSize_d);
    if (!handle->zstd.buffIn_d) {
        goto error;
    }
    handle->zstd.buffOut_d = vmem_calloc(vmem_alloc_default(), handle->zstd.buffOutSize_d);
    if (!handle->zstd.buffOut_d) {
        goto error;
    }

    handle->zstd.input_d.src = handle->zstd.buffIn_d;
    handle->zstd.input_d.size = handle->zstd.buffInSize_d;

    handle->zstd.output_d.dst = handle->zstd.buffOut_d;
    handle->zstd.output_d.size = handle->zstd.buffOutSize_d;

    handle->zstd.dctx = ZSTD_createDCtx();
    if (!handle->zstd.dctx) {
        vapi_info("zstd_init failed: could not create zstd dctx\n");
        goto error;
    }

    ret = ZSTD_CCtx_setParameter(handle->zstd.cctx, ZSTD_c_compressionLevel, clevel);
    if (ZSTD_isError(ret)) {
        vapi_info("zstd_init failed: %s\n", ZSTD_getErrorName(ret));
        goto error;
    }

    ret = ZSTD_CCtx_setParameter(handle->zstd.cctx, ZSTD_c_checksumFlag, 1);
    if (ZSTD_isError(ret)) {
        vapi_info("zstd_init failed: %s\n", ZSTD_getErrorName(ret));
        goto error;
    }

    vapi_debug("[zstd_init]: buffInSize_c: %ld, addr: %p\n", handle->zstd.buffInSize_c, handle->zstd.buffIn_c);
    vapi_debug("[zstd_init]: buffOutSize_c: %ld, addr: %p\n", handle->zstd.buffOutSize_c, handle->zstd.buffOut_c);
    vapi_debug("[zstd_init]: buffInSize_d: %ld, addr: %p\n", handle->zstd.buffInSize_d, handle->zstd.buffIn_d);
    vapi_debug("[zstd_init]: buffOutSize_d: %ld, addr: %p\n", handle->zstd.buffOutSize_d, handle->zstd.buffOut_d);

    return handle;

error:
    zstd_free(handle);
    return NULL;
}

/* return 1 if all data was compressed */
static int zstd_do_compress(ZSTD_CCtx *cctx, ZSTD_inBuffer *input, ZSTD_outBuffer *output, ZSTD_EndDirective mode)
{
    size_t ret;

    if (input->pos > input->size) {
        return -1;
    }

    output->pos = 0;
    ret = ZSTD_compressStream2(cctx, output, input, mode);
    if (ZSTD_isError(ret)) {
        return -1;
    }

    /* return finish stat */
    return (mode == ZSTD_e_end) ? (ret == 0) : (input->pos == input->size);
}

/* return 1 if all data was decompressed */
static int zstd_do_decompress(ZSTD_DCtx *dctx, ZSTD_inBuffer *input, ZSTD_outBuffer *output)
{
    ssize_t ret = 0;

    if (input->pos > input->size) {
        return -1;
    }

    output->pos = 0;
    ret = ZSTD_decompressStream(dctx, output, input);
    if (ZSTD_isError(ret)) {
        return -1;
    }

    return (input->pos == input->size);
}

static int zstd_drop_read_cache(ycfs_handle_t handle)
{
    ZSTD_inBuffer *input;
    ZSTD_outBuffer *output;
    int ret = 0;

    if (!handle) {
        return -1;
    }

    input = &handle->zstd.input_d;
    output = &handle->zstd.output_d;

    vmutex_lock(&handle->lock);
    if (handle->zstd.remaining_d) {
        do {
            ret = zstd_do_decompress(handle->zstd.dctx, input, output);
            if (ret == -1) {
                break;
            }
        } while (!ret);

        handle->zstd.remaining_d = 0;
    }

    handle->zstd.readPos_d = 0;
    output->pos = 0;
    vmutex_unlock(&handle->lock);

    return ret;
}

static int zstd_flush_write_cache(ycfs_handle_t handle, ZSTD_EndDirective mode)
{
    ZSTD_inBuffer *input;
    ZSTD_outBuffer *output;
    int finished;
    int ret = 0;

    if (!handle) {
        return -1;
    }

    input = &handle->zstd.input_c;
    output = &handle->zstd.output_c;

    /* flush remaining data */
    vmutex_lock(&handle->lock);
    if (input->src) {
        do {
            finished = zstd_do_compress(handle->zstd.cctx, input, output, mode);
            if (finished == -1) {
                ret = -1;
                break;
            }

            if (output->pos) {
                ret = vfs_write_non_compressed(handle->fd, output->dst, output->pos);
                if (ret == -1) {
                    break;
                }
            }
        } while (!finished);

        input->size = 0;
        input->pos = 0;


    }
    vmutex_unlock(&handle->lock);

    return ret;
}

static ssize_t zstd_write(ycfs_handle_t handle, const void *buffer, size_t nbytes)
{
    ZSTD_inBuffer *input;
    ZSTD_outBuffer *output;
    int finished = 0;
    ssize_t ret = 0;
    size_t room = 0; /* buffer size to cache user data */
    ssize_t size_return = 0;

    if (!handle || !buffer) {
        return -1;
    }
    if (!nbytes) {
        return 0;
    }

    input = &handle->zstd.input_c;
    output = &handle->zstd.output_c;

    vmutex_lock(&handle->lock);
    while (nbytes) {
        input->src = handle->zstd.buffIn_c;
        room = handle->zstd.buffInSize_c - input->size;

        if (nbytes < room) { /* cache small data */
            memcpy((void *)input->src + input->size, buffer, nbytes);
            input->size += nbytes;
            size_return += nbytes;
            nbytes = 0;
            break;
        } else {
            if (input->size) {
                memcpy((void *)input->src + input->size, buffer, room);
            } else {
                input->src = buffer;
            }

            input->size += room;
            size_return += room;
            buffer += room;
            nbytes -= room;
        }

        do {
            finished = zstd_do_compress(handle->zstd.cctx, input, output, ZSTD_e_continue);
            if (finished == -1) {
                ret = -1;
                break;
            }

            if (output->pos) {
                ret = vfs_write_non_compressed(handle->fd, output->dst, output->pos);
                if (ret == -1) {
                    break;
                }
            }
        } while (!finished);

        input->size = 0;
        input->pos = 0;

        if (ret == -1) {
            break;
        }
    }

    vmutex_unlock(&handle->lock);
    return (ret < 0) ? -1 : size_return;
}

static ssize_t zstd_read(ycfs_handle_t handle, void *buffer, size_t nbytes)
{
    ZSTD_inBuffer *input;
    ZSTD_outBuffer *output;
    ssize_t ret = 0;
    size_t bytes_left = 0; /* decompressed data left in zstd buffer */
    ssize_t size_src = 0; /* compressed data read from src file */
    ssize_t size_return = 0;
    ssize_t size_copy = 0; /* size copied to user */

    if (!handle || !buffer) {
        return -1;
    }
    if (!nbytes) {
        return 0;
    }

    input = &handle->zstd.input_d;
    output = &handle->zstd.output_d;

    vmutex_lock(&handle->lock);
    do {
        /* if already decompressed, copy it directly from output buffer */
        bytes_left = output->pos - handle->zstd.readPos_d;
        size_copy = (bytes_left >= nbytes) ? nbytes : bytes_left;

        if (size_copy) {
            memcpy(buffer, output->dst + handle->zstd.readPos_d, size_copy);
            handle->zstd.readPos_d += size_copy;
            buffer += size_copy;
            size_return += size_copy;
            nbytes -= size_copy;
        }

        if (!nbytes) {
            break;
        }

        handle->zstd.readPos_d = 0;
        output->pos = 0;

        /* start decompressing new data */
        if (!handle->zstd.remaining_d) {
            size_src = vfs_read_non_compressed(handle->fd, (void *)input->src, handle->zstd.buffInSize_d);
            if (size_src == 0) { /* EOF */
                break;
            } else if (size_src == -1) { /* Error */
                ret = -1;
                break;
            }
            input->size = size_src;
            input->pos = 0;
        }

        ret = zstd_do_decompress(handle->zstd.dctx, input, output);
        if (ret == -1) {
            break;
        }

        handle->zstd.remaining_d = !ret;
    } while (nbytes);

    vmutex_unlock(&handle->lock);
    return (ret < 0) ? -1 : size_return;
}

ssize_t vfs_read(int fd, void *buffer, size_t nbytes)
{
    ycfs_handle_t handle = vfs_get_private_data(fd);

    if ((handle) && ((handle->vfs_mode & VFS_MODE_ZSTD) == VFS_MODE_ZSTD)) {
        return zstd_read(handle, buffer, nbytes);
    }

    return vfs_read_non_compressed(fd, buffer, nbytes);
}

ssize_t vfs_write(int fd, const void *buffer, size_t nbytes)
{
    ycfs_handle_t handle = vfs_get_private_data(fd);

    if ((handle) && ((handle->vfs_mode & VFS_MODE_ZSTD) == VFS_MODE_ZSTD)) {
        return zstd_write(handle, buffer, nbytes);
    }

    return vfs_write_non_compressed(fd, buffer, nbytes);
}

int vfs_open(const char *path, int flags, mode_t mode)
{
    int fd;
    ycfs_handle_t handle = NULL;
    int vfs_mode = mode & VFS_MODE_MASK;

    while ((fd = open(path, flags, mode & ~VFS_MODE_MASK)) < 0) {
        /* If not interrupted by signal, don't retry */
        if (errno != EINTR) {
            vapi_info("vfs_open failed: open %s: fd = %d, errno = %d (%s)", path, fd, errno, strerror(errno));
            return fd;
        }
    }

    if (((signed)mode != -1) && ((vfs_mode & VFS_MODE_ZSTD) == VFS_MODE_ZSTD)) {
        if (vfs_private_data_init() < 0) {
            vapi_info("vfs_open failed: private data init failed");
            goto error;
        }

        handle = zstd_init(ZSTD_CLEVEL_DEFAULT);
        if (!handle) {
            vapi_info("vfs_open failed: zstd_init failed");
            goto error;
        } else {
            handle->fd = fd;
            handle->vfs_mode = vfs_mode;
            vfs_set_private_data(fd, handle);
            vapi_debug("[vfs_open]: with ZSTD mode, fd: %d, handle: %p", fd, handle);
        }
    }

    return fd;

error:
    vfs_close_simple(fd);
    return -1;
}

int vfs_close_simple(int fd)
{
    int ret = 0;
    ycfs_handle_t handle = vfs_get_private_data(fd);

    if ((handle) && ((handle->vfs_mode & VFS_MODE_ZSTD) == VFS_MODE_ZSTD)) {
        zstd_flush_write_cache(handle, ZSTD_e_end);
        zstd_free(handle);
        vfs_set_private_data(fd, NULL);
    }

    while ((ret = close(fd)) != 0) {
        /* If not interrupted by signal, don't retry */
        if (errno != EINTR) {
            vapi_info("vfs_close failed: close: fd = %d, errno = %d (%s)", fd, errno, strerror(errno));
            return ret;
        }
    }

    return ret;
}

int vfs_close(int fd)
{
    int ret = 0;

#ifndef OFF_LINE_MIGRATION
#ifdef __linux__
    // prevent loss of data at power failure
    // UBIFS uses write back iso synchronous file access
    while ((ret = fsync(fd)) != 0) {
        /* If not interrupted by signal, don't retry */
        if (errno != EINTR) {
            vapi_info("vfs_close failed: fsync: fd = %d, errno = %d (%s)", fd, errno, strerror(errno));
            return ret;
        }
    }
#endif
#endif

    return vfs_close_simple(fd);
}

off_t vfs_lseek(int fd, off_t offset, int whence)
{
    ycfs_handle_t handle = vfs_get_private_data(fd);

    if ((handle) && ((handle->vfs_mode & VFS_MODE_ZSTD) == VFS_MODE_ZSTD)) {
        zstd_flush_write_cache(handle, ZSTD_e_end);
        zstd_drop_read_cache(handle);
    }

    off_t ret = lseek(fd, offset, whence);

    if (ret == -1) {
        vapi_info("vfs_lseek failed: lseek: fd = %d, errno = %d (%s)", fd, errno, strerror(errno));
    }

    return ret;
}

static ssize_t vfs_read_non_compressed(int fd, void *buffer, size_t nbytes)
{
    ssize_t ret = 0;

    while ((ret = read(fd, buffer, nbytes)) != 0) {
        if (ret == -1) {
            if (errno == EINTR) {
                /* just restart */
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* non-blocking fd: read would block, so return */
                break;
            }

            vapi_info("vfs_read failed: read: fd = %d, errno = %d (%s)", fd, errno, strerror(errno));
            break;
        }
        break;
    }

    return ret;
}

static ssize_t vfs_write_non_compressed(int fd, const void *buffer, size_t nbytes)
{
    ssize_t ret = 0;

    while ((ret = write(fd, buffer, nbytes)) != 0) {
        if (ret == -1) {
            if (errno == EINTR) {
                /* just restart */
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* non-blocking fd: write would block, so return */
                break;
            }

            vapi_info("vfs_write failed: write: fd = %d, errno = %d (%s)", fd, errno, strerror(errno));
            break;
        }
        break;
    }

    return ret;
}

ssize_t vfs_readstring(int fd, char *buffer, size_t size)
{
    char c = '\0';
    ssize_t ret = 0;
    size_t len = 0;

    if ((buffer == NULL) || (size == 0)) {
        vapi_error("vfs_readstring failed: buffer = %p, size = %zu", buffer, size);
        return -1;
    }

    while (len < size - 1) {
        ret = vfs_read(fd, &c, 1);

        if (ret < 0) {
            vapi_warning("vfs_readstring failed: vfs_read: fd = %d", fd);
            return -1;
        }

        if (ret == 0)   /* End of file. */
            break;

        buffer[len++] = c;

        if (c == '\n')
            break;
    }

    buffer[len] = '\0'; /* Write (but do not count) trailing null. */

    return len;
}

ssize_t vfs_readlink(const char *path, char *buf, size_t bufsiz)
{
    if ((buf == NULL) || (bufsiz == 0)) {
        vapi_error("vfs_readlink failed: null or empty buffer");
        return -1;
    } else if ((path == NULL) || (strlen(path) == 0)) {
        vapi_error("vfs_readlink failed: null or empty path");
        return -1;
    }
    return readlink(path, buf, bufsiz - 1);
}

int vfs_symlink(const char *sourcePath, const char *symlinkPath)
{
    if ((sourcePath == NULL) || (strlen(sourcePath) == 0)) {
        vapi_error("vfs_symlink failed: null or empty sourcePath");
        return -1;
    } else if ((symlinkPath == NULL) || (strlen(symlinkPath) == 0)) {
        vapi_error("vfs_symlink failed: null or empty symlinkPath");
        return -1;
    }
    return symlink(sourcePath, symlinkPath);
}

int vfs_stat(const char *path, vfs_stat_t *buf)
{
    return stat(path, buf);
}

int vfs_fstat(int fd, vfs_stat_t *buf)
{
    return fstat(fd, buf);
}

int vfs_lstat(const char *path, vfs_stat_t *buf)
{
    return lstat(path, buf);
}

int vfs_statvfs(const char *file, vfs_statvfs_t *buf)
{
    int rc;

    rc = statvfs(file, buf);
    if (rc != 0) {
        return rc;
    }

#ifdef __linux__
    struct sysinfo sysinfo1;
    if (buf->f_blocks == 0) {  // on linux ramfs, zero means no limits
        rc = sysinfo(&sysinfo1);
        if (rc != 0) {
            return rc;
        }
        buf->f_blocks = sysinfo1.totalram / buf->f_bsize;
        buf->f_bfree  = sysinfo1.freeram  / buf->f_bsize;
        buf->f_bavail = buf->f_bfree;
        buf->f_files  = sysinfo1.totalram / 128;
        buf->f_favail = sysinfo1.freeram  / 128;
    }
#endif
    return rc;
}

int vfs_fstatvfs(int fd, vfs_statvfs_t *buf)
{
    return fstatvfs(fd, buf);
}

void vfs_sync(void)
{
    sync();
}

ssize_t vfs_copy(const char *srcpath, const char *dstpath, mode_t dstmode)
{
    vfs_stat_t statbuf;
    const mode_t srcmode = (mode_t) -1; /* Don't care, ignored anyway. */
    ssize_t ret = 0;

    int src_fd = vfs_open(srcpath, O_RDONLY, srcmode);
    if (src_fd < 0) {
        vapi_warning("vfs_copy failed: vfs_open: path = %s", srcpath);
        ret = -1;
        goto vfs_copy_exit_0;
    }

    if (vfs_fstat(src_fd, &statbuf) < 0) {
        vapi_warning("vfs_copy failed: vfs_fstat: fd = %d", src_fd);
        ret = -1;
        goto vfs_copy_exit_1;
    }

    if (dstmode == (mode_t) -1)
        dstmode = statbuf.st_mode;

    int dst_fd = vfs_open(dstpath, (O_WRONLY | O_CREAT), dstmode);
    if (dst_fd < 0) {
        vapi_warning("vfs_copy failed: vfs_open: path = %s", dstpath);
        ret = -1;
        goto vfs_copy_exit_1;
    }

    ret = sendfile(dst_fd, src_fd, NULL, statbuf.st_size);
    if (ret < 0) {
        vapi_warning("vfs_copy failed: sendfile: out_fd = %d, in_fd = %d, errno = %d (%s)", dst_fd, src_fd, errno, strerror(errno));
        ret = -1;
        goto vfs_copy_exit_2;
    } else if (ret != statbuf.st_size) {
        vapi_warning("vfs_copy failed: nwritten=%zd != nread=%ld", ret, statbuf.st_size);
        ret = -1;
        goto vfs_copy_exit_2;
    }

vfs_copy_exit_2:
    vfs_close(dst_fd);
vfs_copy_exit_1:
    vfs_close(src_fd);
vfs_copy_exit_0:
    return ret;
}

int vfs_unlink(const char *path)
{
    return unlink(path);
}

int vfs_chdir(const char *path)
{
    return chdir(path);
}

int vfs_rmdir(const char *path)
{
    return rmdir(path);
}

int vfs_mkdir(const char *path, mode_t mode)
{
    return mkdir(path, mode);
}

int vfs_opendir(const char *path, vfs_dir_handle_t *handle)
{
    DIR *stream = opendir(path);

    *handle = stream;

    if (stream == NULL)
        return -1;

    return 0;
}

int vfs_readdir(vfs_dir_handle_t handle, char *buffer, size_t size)
{
    DIR *stream = (DIR *) handle;
    struct dirent *result;
    const char *d_name;

    if ((stream == NULL) || (buffer == NULL) || (size == 0))
        return -1;

    /* readdir_r is deprecated and cannot be used. See:
     * http://man7.org/linux/man-pages/man3/readdir_r.3.html
     * readdir on glibc is thread-safe for calls on different directory streams.
     * If vfs_readdir needs to be supported on same directory stream by
     * different threads, then external synchronization should be used.  This
     * means that the caller should not share the used 'handle' between threads.
     */
    errno = 0;
    result = readdir(stream);

    if (result != NULL) {
        /* Not end of directory and no error */
        d_name = result->d_name;
    } else {
        if (errno != 0) {
            /* Error occurred */
            vapi_warning("vfs_readdir failed: %s", strerror(errno));
            return -1;
        } else {
            /* End of directory */
            d_name = "";
        }
    }

    return snprintf(buffer, size, "%s", d_name);
}

int vfs_closedir(vfs_dir_handle_t handle)
{
    DIR *stream = (DIR *) handle;

    if (stream == NULL)
        return -1;

    return closedir(stream);
}

int vfs_file_exists(const char *path)
{
    if (access(path, F_OK) != -1) {
        return 1;
    } else {
        return 0;
    }
}
