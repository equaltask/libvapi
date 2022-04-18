#ifndef __VLIST_H__
#define __VLIST_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file vlist.h
 * \brief a very basic list implementation
 *
 * with this simple implementation,
 * ylist_t ylist must be the first element in your struct to make it
 * a listable element
 */


#define UNIQUEPASTE(x, y)   x ## y
#define UNIQUEPASTE2(x, y)  UNIQUEPASTE(x, y)
#define UNIQUE              UNIQUEPASTE2(_tmp_, __LINE__)

#define vlist_foreach(lst,lp) __typeof__(lp) UNIQUE; \
            for (lp = (lst)->next; UNIQUE = lp ? lp->next : NULL, lp && (lp != (lst)); lp = UNIQUE)

#define vlist_get_head(lst,hd) hd = (__typeof__(hd)) (lst)->next

#define vlist_get_tail(lst,hd) hd = (__typeof__(hd)) (lst)->prev

#define ELIST_INITIALIZER(list) { .next = &(list), .prev = &(list) }

typedef struct vlist {
    struct vlist *next, *prev;
} vlist_t;

static inline void vlist_init(vlist_t *vlist)
{
    vlist->next = vlist;
    vlist->prev = vlist;
}

static inline int vlist_is_empty(vlist_t *vlist)
{
    return vlist->next == vlist;
}

static inline void vlist_insert(vlist_t *entry, vlist_t *before, vlist_t *after)
{
    before->next = entry;
    after->prev = entry;
    entry->next = after;
    entry->prev = before;
}

static inline void vlist_add_head(vlist_t *vlist, vlist_t *entry)
{
    vlist_insert(entry, vlist, vlist->next);
}

static inline void vlist_add_tail(vlist_t *vlist, vlist_t *entry)
{
    vlist_insert(entry, vlist->prev, vlist);
}

static inline int evist_is_head(vlist_t *vlist, vlist_t *entry)
{
    return vlist->next == entry;
}

static inline int vlist_is_tail(vlist_t *vlist, vlist_t *entry)
{
    return entry->next == vlist;
}

static inline void vlist_append_list_to_list(vlist_t *list_being_appended, vlist_t *list_being_emptied)
{
    if (vlist_is_empty(list_being_emptied))
        return;

    list_being_appended->prev->next = list_being_emptied->next;
    list_being_emptied->next->prev = list_being_appended->prev;
    list_being_emptied->prev->next = list_being_appended;
    list_being_appended->prev = list_being_emptied->prev;
    list_being_emptied->next = list_being_emptied;
    list_being_emptied->prev = list_being_emptied;
}

static inline void vlist_delete(vlist_t *entry)
{
    if (entry->next && entry->prev) {
        entry->next->prev = entry->prev;
        entry->prev->next = entry->next;
    }
    entry->next = NULL;
    entry->prev = NULL;
}

size_t vlist_count(vlist_t *vlist);


#ifdef __cplusplus
}
#endif

#endif  /*__ELIST_H__*/
