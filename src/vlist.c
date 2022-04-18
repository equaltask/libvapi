#include <libvapi/vlist.h>

size_t vlist_count(vlist_t *vlist)
{
    size_t count = 0;
    vlist_t *lp = NULL;

    vlist_foreach(vlist, lp)
    count++;

    return count;
}

