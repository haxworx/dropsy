#include "scripts.h"
#include "monitor.h"

#define CMD_LEN 1024

int do_add(void *data)
{
        file_t *f = data;
        char cmd[CMD_LEN];
        snprintf(cmd, sizeof(cmd), "scripts/add.sh '%s'", f->path);
        return system(cmd);
}

int do_del(void *data)
{
        file_t *f = data;
        char cmd[CMD_LEN];
        snprintf(cmd, sizeof(cmd), "scripts/del.sh '%s'", f->path);
        return system(cmd);
}

int do_mod(void *data)
{
        file_t *f = data;
        char cmd[CMD_LEN];
        snprintf(cmd, sizeof(cmd), "scripts/mod.sh '%s'", f->path);
        return system(cmd);
}

