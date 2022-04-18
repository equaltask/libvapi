#include <stdio.h>
#include <stdlib.h>
#include <libvapi/vevent.h>
#include <libvapi/vtimer.h>
#include <libvapi/vmem.h>
#include <libvapi/vloop.h>

#include "vloop_internal.h"

struct vevent {
    vevent_cb_t cb;
    void *ctxt;
    vtimer_t timer;
    vloop_action_t cancel_action;
    vmem_alloc_t mempool;
};

static void action_cancel_cb(vloop_action_t action, void *ctxt)
{
    vevent_t *event = (vevent_t *) ctxt;

    // make a local copy of the needed data after the delete.
    vevent_cb_t lcb =  event->cb;
    void *lctxt = event->ctxt;

    // cb funtion will cleanup the ctxt and also release the memory.
    // Since the vevent data might be stored in the same block (optimisation)
    // the event needs to be deleted first.
    vevent_delete(event);

    lcb(VEVENT_CANCEL, lctxt);
}

static void timeout_cb(vtimer_t timer, void *ctxt)
{
    vevent_t *event = (vevent_t *) ctxt;

    // make a local copy of the needed data after the delete.
    vevent_cb_t lcb =  event->cb;
    void *lctxt = event->ctxt;

    // cb funtion will cleanup the ctxt and also release the memory.
    // Since the vevent data might be stored in the same block (optimisation)
    // the event needs to be deleted first.
    vevent_delete(event);

    lcb(VEVENT_TIMEOUT, lctxt);
}

uint32_t vevent_getsize()
{
    return sizeof(struct vevent);
}

vevent_t *vevent_construct(void *ptr, vevent_cb_t cb, void *ctxt, vmem_alloc_t mempool)
{
    vevent_t *event = (vevent_t *)ptr;

    event->cb = cb;
    event->ctxt = ctxt;
    event->timer = NULL;
    event->cancel_action = NULL;
    event->mempool = mempool;

    return event;
}

vevent_t *vevent_new(vevent_cb_t cb, void *ctxt)
{
    vmem_alloc_t mempool = vmem_alloc_default();
    void *ptr = vmem_malloc(mempool, sizeof(vevent_t));
    if (ptr == NULL)
        return NULL;

    return vevent_construct(ptr, cb, ctxt, mempool);
}

void vevent_delete(vevent_t *event)
{
    if (event == NULL)
        return;

    if (event->timer != NULL)
        vtimer_delete(event->timer);

    if (event->cancel_action != NULL)
        vloop_action_delete(event->cancel_action);

    // memory has to be released as a last action.
    if (event->mempool)
        vmem_free(event->mempool, event);
}

void vevent_cancel(vevent_t *event)
{
    if (event == NULL)
        return;

    if (event->cancel_action != NULL)
        return;

    if (event->timer != NULL) {
        vtimer_delete(event->timer);
        event->timer = NULL;
    }

    event->cancel_action =  vloop_action_create(action_cancel_cb, (void *)event);
}

int vevent_set_timeout(vevent_t *event, int ms)
{
    if (event == NULL)
        return -1;

    if (event->cancel_action != NULL)
        return -1;

    if (event->timer != NULL)
        vtimer_delete(event->timer);

    if (vlog_level_enabled_on_vapi_component(VTIMER_INDEX)) {
        char span_name[VLOG_MAX_SPAN_NAME] = {0};
        snprintf(span_name, VLOG_MAX_SPAN_NAME - 1, "vtimer_vevent_set_timeout_%p", event);

        vlog_start_parent_span(span_name);

        event->timer = vtimer_start_timeout_ot(timeout_cb, ms, event, vlog_get_span_context(span_name), vlog_get_span_context_size(span_name));

        vlog_finish_span(span_name);
    } else {
        event->timer = vtimer_start_timeout(timeout_cb, ms, event);
    }

    if (event->timer == NULL)
        return -1;

    return 0;
}
