#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libvapi/vlog.h>
#include <libvapi/vdbg.h>
#include <libvapi/vtimer.h>
#include <libvapi/vloop.h>
#include <libvapi/vtnd.h>


static void foo_dbg_help_test(void *ctx)
{
    vdbg_printf("foo debug Help: test\n");
    vdbg_printf("--------------------\n");
    vdbg_printf("\n");
    vdbg_printf("Usage: foo bar1|bar2 ...\n");
    vdbg_printf("\n");
}


void bar1_help(void *ctx)
{
    vdbg_printf("module tnd bar1 help sample\n");
}


void bar2_help(void *ctx)
{
    vdbg_printf("module tnd bar2 help sample\n");
    vdbg_printf("cmd expects args format \"%%s %%d %%d %%f\"\n");
}


static int count;

static void timer_cb(vtimer_t timer, void *ctx)
{
    vtimer_delete(timer);
    count++;

    // Restore the debug context
    vtnd_set_context(ctx);
    vdbg_printf("timer expiry %d\n", count);
    if (count == 5) {
        // After 5 expiries, we consider the command completed and close the context
        vtnd_finish_context(ctx,0);
    }
    else {
        // Not finished yet, set context to NULL and restart the timer
        vtnd_set_context(NULL);
        vtimer_start_timeout(timer_cb,1000, ctx);
    }
}

int bar1_cmd(char *cmd, char *args, void *ctx)
{
    // This is an example of an async debug command.
    // It will print a part of the output here and then starts a timer
    // in which the output will proceed.
    // If the timer is expired 5 times, the debug command is finished the the context is closed.
    vdbg_printf("hello bar1\n");

    void *tnd_ctxt = vtnd_get_context();
    vtimer_start_timeout(timer_cb,1000, tnd_ctxt);

    // It is very important to make the context NULL since otherwise it will not be possible
    // for yAPI to detect ydbg_printf invocations from application (none-debug) context.
    vtnd_set_context(NULL);

    return 0;
}


int bar2_cmd(char *cmd, char *args, void *ctx)
{
    char string[32] = "";
    int number1, number2;
    float number3;

    vdbg_printf("module tnd bar2 cmd sample\n");
    vdbg_printf("cmd expects args format \"%%s %%d %%d %%f\"\n");

    vdbg_scan_args(args, "%s %d %d %f", string, &number1, &number2, &number3);

    vdbg_printf("passed args: %s %d %d %f\n", string, number1, number2, number3);

    return 0;
}


int main(int argc, char* argv[])
{
    vdbg_link_module("foo", foo_dbg_help_test, NULL);

    vdbg_link_cmd("foo", "bar1", bar1_help, bar1_cmd, NULL);
    vdbg_link_cmd("foo", "bar2", bar2_help, bar2_cmd, NULL);

    return 0;
}
