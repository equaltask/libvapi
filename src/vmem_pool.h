#ifndef __VMEM_POOL_H__
#define __VMEM_POOL_H__

#include <stddef.h>
#include <libvapi/vtypes.h>
#include <libvapi/vmem.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *vmem_pool_t;

vmem_pool_t vmem_pool_create(size_t size, ulong_t nr_elem);
void *vmem_pool_alloc(vmem_pool_t pool, size_t size);
void *vmem_pool_free(vmem_pool_t pool, void *ptr);
void vmem_pool_print(vmem_pool_t pool,
                     void (*print_cb)(void *cb_arg, const char *string),
                     void *cb_arg);

void vmem_alloc_print(vmem_alloc_t allocator,
                      void (*print_cb)(void *cb_arg, const char *string),
                      void *cb_arg);

typedef struct vmem_alloc_iter {
    vmem_alloc_t curr;
} vmem_alloc_iter_t;

void vmem_alloc_iter_init(vmem_alloc_iter_t *iter);
vmem_alloc_t vmem_alloc_iter_next(vmem_alloc_iter_t *iter);

void vmem_install_default_trace_callback(vmem_cb_t log_cb);
void vmem_install_default_error_callback(vmem_cb_t err_cb);
int vmem_trim_start(void);

#ifdef __cplusplus
};
#endif

#endif
