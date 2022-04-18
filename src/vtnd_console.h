
#ifndef __VTND_CONSOLE_H__
#define __VTND_CONSOLE_H__

#include <libvapi/vtnd.h>

#ifdef __cplusplus
extern "C"
{
#endif

int vtnd_console_create(const char *pty_path);
int vtnd_console_get_connection(struct tnd_connection **ref);
int vtnd_console_get_fd(void);
void vtnd_console_reset(void);

#ifdef __cplusplus
}
#endif

#endif
