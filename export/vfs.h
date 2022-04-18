#ifndef __VFS_H__
#define __VFS_H__

#include <libvapi/vtypes.h>
#include <libvapi/vevent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

/*!
 * \file vfs.h
 * \brief Interface definition for the file system interface of the libvapi platform.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stat vfs_stat_t;
typedef struct statvfs vfs_statvfs_t;
typedef void *vfs_dir_handle_t;

/* extended mode for vfs */
#define VFS_MODE_MASK    0xF0000000
#define VFS_MODE_ZSTD    0x10000000 /* enable zstd compression */

/*!
 * \brief Type of filesystem events that can be monitored.
 */
typedef enum {
    VFS_EVENT_NONE = 0,
    VFS_EVENT_CREATE = 1, /*!< Creation of an entry on the filesystem */
    VFS_EVENT_MODIFY = 2,
    VFS_EVENT_DELETE = 4
} vfs_monitor_event_t;

/*!
 * \brief Type of callback used by vfs_monitor.
 * \param reason Reason why the callback is called.
 * \param type_mask Bitwise OR-ed mask of vfs_monitor_event_t's that occured.
 * \param name Name of the file that triggered the event.
 * \param ctxt User context given to vfs_monitor.
 */
typedef void (*vfs_monitor_cb_t)(vevent_reason_t reason, uint32_t type_mask, const char *name, void *ctxt);

/*!
 * \brief Open a file.
 *
 * This operation searches, starting from the workdirectory (eatpath).
 * When the specified file is found, the file inode is copied in the inode table
 * (in memory). A new file descriptor is allocated and will be filled with the
 * correct file data (if the file is not yet opened). The mode is checked.
 * If the value is not valid, the error EINVAL is returned.
 * Note that write actions are using a cache. To store the data on the device,
 * the cache must be synced.
 * \param path IN Path must indicate the correct file name (the name can include
 * a directory path).
 * \param flags IN Flags indicate the properties of the file to be opened.
 * A correct value can be obtained through an OR of options:
 * <ul>
 * <li> O_WRONLY: open for writing
 * <li> O_RDONLY: open for reading
 * <li> O_RDWR: open for reading and writing
 * <li> O_CREAT: if the file exists it is truncated and opened, otherwise it is created
 * <li> O_TRUNC: the file is truncated to zero
 * <li> O_APPEND: the seek pointer is set to the end of the file prior to each write
 * </ul>
 * \param mode IN Mode is used when the file is created to set the file permissions to
 * the owner, group and others.
 * \return file descriptor on success, error -1 on failure.
 * \sa vfs_close, vfs_sync
 */
int vfs_open(const char *path, int flags, mode_t mode);

/*!
 * \brief Close a file descriptor.
 *
 * The difference in between this call and vfs_close() is the file synchronization
 * which takes place for vfs_close(); but not for this call. This call simply closes
 * the file descriptor. This may be required when dealing with file descriptors that
 * do not map on a file system backed file.
 * \param fd IN File descriptor of the file to be closed.
 * \return 0 on sucess, error -1 on failure
 * \sa vfs_open
 */
int vfs_close_simple(int fd);

/*!
 * \brief Close a file.
 *
 * When a specific file is no longer needed it can be closed. With this action
 * you remove the file descriptor from the file descriptor table. The file
 * will be synced to its storage medium as part of this call.
 * \param fd IN File descriptor of the file to be closed.
 * \return 0 on success, error -1 on failure.
 * \sa vfs_open, vfs_sync, vfs_close_simple
 */
int vfs_close(int fd);

/*!
 * \brief Put the file pointer at specified position.
 *
 * Set the file pointer (read/write position) to the requested value.
 * \param fd IN File descriptor to specify the file.
 * \param offset IN value to be added to the reference point.
 * \param whence IN point specified by special code:
 * <ul>
 * <li> SEEK_SET: Offset will be referred to from the beginning of the file.
 * <li> SEEK_CUR: Offset will be referred to from current position.
 * <li> SEEK_END: Offset will be referred to from end of file (file size).
 * </ul>
 * \return file position on success, error -1 on failure.
 * \sa vfs_read, vfs_write
 */
off_t vfs_lseek(int fd, off_t offset, int whence);

/*!
 * \brief Read a number of bytes from a file.
 *
 * Attempt to read the requested number of bytes from the file specified by the file descriptor
 * and copy them into the buffer indicated by the buffer pointer. Any user defined
 * device can be attached to a file descriptor, enabling the user to read from this.
 * \param fd IN File descriptor to specify the file.
 * \param buffer OUT Pointer to the data-location.
 * \param nbytes IN Number of bytes to be read.
 * \return Number of read bytes on success, error -1 on failure.
 * \sa vfs_open, vfs_lseek, vfs_write
 */
ssize_t vfs_read(int fd, void *buffer, size_t nbytes);

/*!
 * \brief Write a number of bytes to a file.
 *
 * Write up to the requested number of bytes to the file (normally in cache buffer). The file
 * descriptor indicates the file to be written to. The data is found at the location
 * referred to by the buffer pointer.
 * The file descriptor can also refer to any user defined device.
 * Normal applications are supposed to use ylog_print or ERROR.
 * \param fd IN File descriptor to specify the file.
 * \param buffer IN Pointer to the data-location.
 * \param nbytes IN Number of bytes to be written.
 * \return Number of written bytes on success, error -1 on failure.
 * \sa vfs_open, vfs_lseek, vfs_sync
 */
ssize_t vfs_write(int fd, const void *buffer, size_t nbytes);

/*!
 * \brief Read a string from a text file.
 *
 * Read at most size minus one characters from the text file specified by the file descriptor and
 * write them into the string buffer. Reading from file stops after a newline or if EOF is encountered.
 * If a newline is read, it is stored into the buffer as well.
 * The string buffer will always be null terminated, unless an error occurs (see return value).
 * \param fd IN File descriptor to specify the file.
 * \param buffer OUT Pointer to the string buffer.
 * \param size IN Size of the string buffer.
 * \return 0 or positive on success, error -1 on failure.
 * If the return value is zero, EOF is reached and an empty string is returned in the string buffer.
 * A positive return value denotes the exact number of bytes read from the file and stored into the
 * string buffer. This includes an eventual newline, but excludes the terminating null character.
 * \sa vfs_lseek, vfs_read, vfs_write
 */
ssize_t vfs_readstring(int fd, char *buffer, size_t size);

/*!
 * \brief Read the value of a symbolic link.
 *
 * Readlink places the contents of the symbolic link path in the buffer buf, which has size bufsiz.
 * \param path IN Path to the symbolic link
 * \param buf  OUT Pointer to the data-location.
 * \param bufsiz IN Size of the buffer.
 * \return Number of bytes placed in buf on success, error -1 on failure.
 * \sa vfs_lstat, vfs_symlink
 */
ssize_t vfs_readlink(const char *path, char *buf, size_t bufsiz);

/*!
 * \brief Create a symbolic link
 *
 * Symlink creates symbolic link named symlinkPath which contains the string sourcePath.
 * \param sourcePath IN Path to the file that the symlink points to.
 * \param symlinkPath IN Path to the symlink.
 * \return 0 on success, error -1 on failure.
 * \sa vfs_lstat, vfs_readlink
 */
int vfs_symlink(const char *sourcePath, const char *symlinkPath);

/*!
 * \brief Obtain information from specified file.
 *
 * The information concerning this file will be retrieved and stored in buf.
 * \param path IN File to be inspected.
 * \param buf OUT Pointer to buffer to return the status information.
 * Members of the structure (defined in stat.h):
 * st_dev: device number
 * st_ino: file inode number
 * st_mode: file mode (permissions)
 * st_nlink: number of links to this file
 * st_uid: owner userid
 * st_gid: owner groupid
 * st_rdev: device ID (if special file)
 * st_size: file size
 * st_blksize: blocksize for file system I/O
 * st_blocks: number of 512B blocks allocated
 * st_atime: last access time
 * st_mtime: last modification time
 * st_ctime: last status change time
 * \return 0 on success, error -1 on failure.
 * \sa vfs_fstat
 */
int vfs_stat(const char *path, vfs_stat_t *buf);

/*!
 * \brief Obtain information from specified file.
 *
 * Same function as vfs_stat, but this function uses a file descriptor of an open file.
 * \param fd IN File descriptor to specify the file.
 * \param buf OUT Pointer to status buffer (see vfs_stat).
 * \return 0 on success, error -1 on failure.
 * \sa vfs_stat
 */
int vfs_fstat(int fd, vfs_stat_t *buf);

/*!
 * \brief Obtain information from specified file, coping with symbolic links.
 *
 * Same function as vfs_stat, except that if path is a symbolic link,
 * then the link itself is stat-ed
 * \param path IN File to be inspected.
 * \param buf OUT Pointer to status buffer (see vfs_stat).
 * \return 0 on success, error -1 on failure.
 * \sa vfs_stat
 */
int vfs_lstat(const char *path, vfs_stat_t *buf);

/*!
 * \brief Get the FS status.
 *
 * Get the status of the FS where the specified file/directory is located.
 * \param path IN Specify the FS using a file/directory on it.
 * \param buf OUT Pointer to buffer to return the status information.
 * Members of the structure (defined in statvfs.h):
 * f_type: type of file system
 * f_bsize: block size
 * f_blocks: number of blocks
 * f_bfree: number of free blocks
 * f_bavail: number of free blocks avail to non-superuser
 * f_files: number of file nodes
 * f_ffree: number of free file nodes
 * f_fsid: file system id
 * f_namelen: max length of filenames
 * \return 0 on success, error -1 on failure.
 * \sa vfs_fstatvfs
 */
int vfs_statvfs(const char *path, vfs_statvfs_t *buf);

/*!
 * \brief Get the FS status.
 *
 * Same function as vfs_statvfs, but this function uses a file descriptor of an open file.
 * \param fd IN File descriptor (the FS on which the file is located).
 * \param buf OUT Pointer to status buffer (see vfs_statvfs).
 * \return 0 on success, error -1 on failure.
 * \sa vfs_statvfs
 */
int vfs_fstatvfs(int fd, vfs_statvfs_t *buf);

/*!
 * \brief Sync cache.
 *
 * When a write-call is used, the data is not directly written onto the device.
 * This is for a number of reasons e.g. performance speed, optimize the life time
 * of the device (reduce the number of write cycles on the device).This gives the
 * problem that the data is still in RAM. So when power-down occurs, the data is
 * lost. To sync the data to the device, usage of the sync-instruction is required.
 * It is advisable not to use the sync too often, because it increases the number
 * of write cycles, so the lifetime will decrease.
 * \sa vfs_write
 */
void vfs_sync(void);

/*!
 * \brief Copy the contents from a file to a destination file.
 *
 * Copy the file contents from specified source file to destination file.
 * If destination file didn't exist, it is created.
 * \param srcpath IN Path to source file.
 * \param dstpath IN Path to destination file.
 * \param dstmode IN Path to destination file.
 * \param dstmode IN Set the destination file permissions for the owner, group and others
 * (specify (mode_t) -1 to inherit the permissions of the source file).
 * \return Number of written bytes on success, error -1 on failure.
 * \sa vfs_open, vfs_read, vfs_write
 */
ssize_t vfs_copy(const char *srcpath, const char *dstpath, mode_t mode);

/*!
 * \brief Remove this link and possibly the file it refers to.
 *
 * Remove a link to a file. If this is the last link to an existing file, the file will be removed.
 * \param path IN Path to file to be deleted.
 * \return 0 on success, error -1 on failure.
 * \sa vfs_open
 */
int vfs_unlink(char const *path);

/*!
 * \brief Change working directory.
 *
 * Change the working directory if the specified path is a directory and the
 * directorypermissions enable you to access.
 * \param path IN A directory path, referenced to from the current working directory,
 * specifying a new working directory. The usage of normal directory redirection
 * ("." for current directory,".." for parent directory, and "/" for root directory
 * and directory name separator) are possible.
 * \return 0 on success, error -1 on failure.
 * \sa vfs_mkdir, vfs_rmdir
 */
int vfs_chdir(const char *path);

/*!
 * \brief Remove a directory.
 *
 * Remove a directory. To be able to remove a directory, the directory must be empty.
 * First the directory is deleted, then the directory-entry in the parent directory
 * is removed.
 * \param path IN The directory to be removed..
 * \return 0 on success, error -1 on failure.
 * \sa vfs_chdir, vfs_mkdir
 */
int vfs_rmdir(const char *path);

/*!
 * \brief Make a new directory.
 *
 * Create a new directory in the current working directory. The specified mode is
 * for protection (owner - user - other).
 * \param path IN To specify new directory.
 * \param mode IN Set the file permissions for the owner, group and others.
 * \return 0 on success, error -1 on failure.
 * \sa vfs_chdir, vfs_rmdir
 */
int vfs_mkdir(const char *path, mode_t mode);

/*!
 * \brief Open a directory.
 *
 * This function opens a directory stream corresponding to the directory named by the path argument.
 * The directory stream is positioned at the first directory entry.
 * \param path IN The directory to be opened.
 * \param handle OUT Directory stream handle.
 * \return 0 on success, error -1 on failure.
 * \sa vfs_readdir, vfs_closedir
 */
int vfs_opendir(const char *path, vfs_dir_handle_t *handle);

/*!
 * \brief Read a directory.
 *
 * This function returns the name of the directory entry at the current position in the directory
 * stream specified by the handle argument, and positions the directory stream at the next entry.
 * On success a null terminated directory entry name is returned in buffer.
 * Upon reaching the end of the directory stream, an empty string is returned in buffer.
 *
 * \note The input parameter handle should NOT be shared between threads, or
 * race conditions will occur.
 *
 * \param handle IN Directory stream handle.
 * \param buffer OUT Directory entry name at the current position.
 * \param size IN Size of buffer.
 * \return 0 or positive on success, error -1 on failure.
 * A positive value denotes the size of the current directory entry (without the trailing '\0').
 * If this positive return value is greater than or equal to the size argument, than the output
 * in the buffer argument is truncated.
 * If the return value is zero, the end of the directory stream is reached and
 * an empty string is returned in the buffer argument.
 * \sa vfs_opendir, vfs_closedir
 */
int vfs_readdir(vfs_dir_handle_t handle, char *buffer, size_t size);

/*!
 * \brief Close a directory.
 *
 * This function closes the directory stream referred to by the argument handle.
 * \param handle IN Directory stream handle.
 * \return 0 on success, error -1 on failure.
 * \sa vfs_opendir, vfs_readdir
 */
int vfs_closedir(vfs_dir_handle_t handle);

/*!
 * \brief Monitor a path for filesystem events.
 * The current implementation only supports file creation events. The path should be an
 * existing directory. When a file is created in this directory, the given callback is
 * called.
 * \param path Path to be monitored.
 * \param type_mask Bitwise OR-ed vfs_monitor_event_t's to be reported.
 * \param cb Callback to be called when an event occurs.
 * \param ctxt Context pointer to be given to the callback
 * \return NULL on failure, a yevent in case of success. The yevent can be used to
 *         stop the monitoring (with yevent_cancel).
 */
vevent_t *vfs_monitor(const char *path, uint32_t type_mask, vfs_monitor_cb_t cb, void *ctxt);

/*!
 * \brief Check whether a file exists on the filesystem.
 * \param path Path to the file, absolute or relative to current directory
 * \return 1 if the file exists, 0 otherwise
 */
int vfs_file_exists(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* __VFS_H__ */
