#ifndef __VMEM_H__
#define __VMEM_H__

#include <libvapi/vtypes.h>

/*!
 * \file vmem.h
 * \brief Interface definition for the memory management interface of the yAPI platform.
 * \example default_allocator.c
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef void *vmem_alloc_t;

typedef uint32_t vmem_options_t;
enum {
    vmem_options_trace    = 0x10001,
    vmem_options_no_trace = 0x10000,
    vmem_options_error    = 0x20002,
    vmem_options_no_error = 0x20000
};

typedef enum {
    vmem_locktype_mutex,
    vmem_locktype_none
}
vmem_locktype_t;

typedef enum {
    vmem_error_success = 0,
    vmem_error_failure = 1
}
vmem_error_t;

/*!
 * \brief   Return system allocator.
 *
 * Returns a reference to the system allocator (::malloc, ::free, ::new, ::delete, ...).
 * Can only be used to install error and trace callback functions or to enable tracing.
 *
 * \return                  Reference to system allocator (always non-NULL).
 * \sa      vmem_alloc_default vmem_alloc_create_pool
 */
vmem_alloc_t vmem_alloc_system(void);

/*!
 * \brief   Return default allocator.
 *
 * Returns a reference to the general purpose allocator (default allocator).
 *
 * \return                  Reference to default allocator (always non-NULL).
 * \sa      vmem_alloc_system vmem_alloc_create_pool
 */
vmem_alloc_t vmem_alloc_default(void);

/*!
 * \brief   Create pool allocator.
 *
 * Creates an allocator associated with a memory pool of nr_elem blocks of (an arbitrary) size.
 * This function assures that alignment constraints are met for each block.
 * Returns a reference to the allocator on success, or NULL otherwise.
 *
 * \param   size        IN  Number of bytes per block.
 * \param   nr_elem     IN  Number of blocks.
 * \param   locktype    IN  Type of lock to be used to be thread-safe, vmem_locktype_none otherwise.
 * \return                  Reference to allocator, or NULL on error.
 * \sa      vmem_alloc_system vmem_alloc_default
 */
vmem_alloc_t vmem_alloc_create_pool(size_t size, ulong_t nr_elem, vmem_locktype_t locktype);

/*!
 * \brief   Delete pool allocator.
 *
 * Destroys a pool allocator together with its associated memory pool.
 *
 * \param   allocator   IN  Allocator to delete.
 * \return                  On succes vmem_error_success, or vmem_error_failure otherwise.
 * \sa      vmem_alloc_create_pool
 */
vmem_error_t vmem_alloc_delete_pool(vmem_alloc_t allocator);

/*!
 * \brief   Allocate memory.
 *
 * This operation allocates 'size' bytes memory from the provided allocator.
 * If no memory can be allocated anymore, a NULL pointer is returned.
 * Its functionality corresponds to the standard malloc call.
 *
 * \param   allocator   IN  Allocator to allocate memory from.
 * \param   size        IN  Number of bytes to allocate.
 * \return                  Pointer to allocated memory, or NULL on error.
 * \sa      vmem_free
 */
void *vmem_malloc(vmem_alloc_t allocator, size_t size);

/*!
 * \brief   Allocate memory and set to 0.
 *
 * This operation allocates 'size' bytes memory from the provided allocator and sets it to zero.
 * If no memory can be allocated anymore, a NULL pointer is returned.
 * Its functionality corresponds to the standard calloc call, but without nmemb support.
 *
 * \param   allocator   IN  Allocator to allocate memory from.
 * \param   size        IN  Number of bytes to allocate.
 * \return                  Pointer to allocated memory, or NULL on error.
 * \sa      vmem_free
 */
void *vmem_calloc(vmem_alloc_t allocator, size_t size);

/*!
 * \brief   Free memory.
 *
 * This operation releases the memory pointed to by ptr to the provided allocator.
 * Its functionality corresponds to the standard malloc call, but with a return value.
 *
 * \param   allocator   IN  Allocator to release memory to.
 * \param   ptr         IN  Memory to be released.
 * \return                  NULL, or ptr on error.
 * \sa      vmem_malloc vmem_calloc
 *
 * \note    Freeing a block of memory to the default allocator always returns NULL.
 *          However, freeing a block of memory to a wrong pool allocator will return non-NULL!
 */
void *vmem_free(vmem_alloc_t allocator, void *ptr);

/*!
 * \brief   Allocate a piece of aligned memory.
 *
 * This operation allocates and aligns 'size' bytes of memory from the provided allocator.
 * If no memory can be allocated anymore, a NULL pointer is returned.
 * Its functionality corresponds to the standard memalign call.
 *
 * This operation is not supported on a pool allocator.
 *
 * \param   allocator   IN  Allocator to allocate memory from.
 * \param   align       IN  Memory ptr will be aligned on 'align' bytes.
 * \param   size        IN  Number of bytes to allocate.
 * \return                  Pointer to allocated memory, or NULL on error.
 * \sa      vmem_free
 */
void *vmem_memalign(vmem_alloc_t allocator, size_t align, size_t size);

/*!
 * \brief   Reallocate a piece of memory.
 *
 * This operation reallocates 'size' bytes of memory from the provided allocator.
 * If no memory can be allocated anymore, a NULL pointer is returned.
 * Its functionality corresponds to the standard realloc call.
 *
 * This operation is not supported on a pool allocator.
 *
 * \param   allocator   IN  Allocator to allocate memory from.
 * \param   ptr         IN  Pointer to memory that will be realloc'ed.
 * \param   size        IN  Number of bytes to allocate.
 * \return                  Pointer to allocated memory, or NULL on error.
 * \sa      vmem_free
 */
void *vmem_realloc(vmem_alloc_t allocator, void *ptr, size_t size);

/*!
 * \brief   Release free memory from the top of the heap.
 *
 * This operation attempts to release free memory at the top of the heap by calling sbrk.
 * Standard glibc allocator doesn't return memory to linux OS automatically, so by using
 * this function a forced release of free memory is triggered.
 *
 * \return      Returns 1 if memory was actually released back to the system,
 *              or 0 if it was not possible to release any memory.
 * \sa      vmem_malloc vmem_free
 */
int vmem_malloc_trim(void);

/*!
 * \brief   Set the options of an allocator.
 *
 * This operation sets the options on the provided allocator.
 *
 * \param   allocator   IN  Allocator to set the options on.
 * \param   options     IN  Options to set.
 * \return                  On succes vmem_error_success, or vmem_error_failure otherwise.
 * \sa      vmem_getoption
 */
vmem_error_t vmem_setoption(vmem_alloc_t allocator, vmem_options_t options);


/*!
 * \brief   Get the options of an allocator.
 *
 * This operation gets the options of the provided allocator.
 *
 * \param   allocator   IN  Allocator to get the options from.
 * \param   options     OUT Options currently set.
 * \return                  On succes vmem_error_success, or vmem_error_failure otherwise.
 * \sa      vmem_setoption
 */
vmem_error_t vmem_getoption(vmem_alloc_t allocator, vmem_options_t *options);

typedef void (*vmem_cb_t)(const char *string);

/*!
 * \brief   Install/override callback function to do tracing.
 *
 * This operation overrides the default trace callback function.
 *
 * \param   allocator   IN  Allocator to install callback function for.
 * \param   log_cb      IN  New callback function.
 * \return                  Old callback function.
 * \sa      vmem_install_error_callback
 */
vmem_cb_t vmem_install_trace_callback(vmem_alloc_t allocator, vmem_cb_t log_cb);

/*!
 * \brief   Install/override callback function to call in case of an error.
 *
 * This operation overrides the default error callback function.
 *
 * \param   allocator   IN  Allocator to install callback function for.
 * \param   err_cb      IN  New callback function.
 * \return                  Old callback function.
 * \sa      vmem_install_trace_callback
 */
vmem_cb_t vmem_install_error_callback(vmem_alloc_t allocator, vmem_cb_t err_cb);

#ifdef __cplusplus
};
#endif

#endif
