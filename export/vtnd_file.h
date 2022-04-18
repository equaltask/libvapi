
#ifndef __VTND_FILE_H__
#define __VTND_FILE_H__

#ifdef __cplusplus
extern "C"
{
#endif

int vtnd_file_init(const char *path);
int vtnd_file_run_cmd(const char *path, const char *mod, int len, const char *filter);

#ifdef __cplusplus
}
#endif

#endif
