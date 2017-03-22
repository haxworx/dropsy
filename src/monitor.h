#if ! defined(__MONITOR_H__)

#define PROGRAM_NAME "dropsy"
#define __MONITOR_H__
#define _DEFAULT_SOURCE
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>
#include <signal.h>

#define SLASH '/'

#define DIRS_MAX 256

#define MONITOR_NONE 0
#define MONITOR_ADD 1 
#define MONITOR_DEL 2 
#define MONITOR_MOD 3

typedef struct file_t file_t;
struct file_t {
	char *path;
	int size;
	int mtime;
	int changed;
	file_t *next;
};

typedef int (*callback)(void *data);

typedef int (*fn_callback_set)(void *self, int type, callback);
typedef int (*fn_watch_add)(void *self, const char * path);
typedef int (*fn_init)(void *self, char *cmd_string);
typedef int (*fn_watch)(void *self, int poll_interval);
typedef int (*fn_authenticate)(void *self);
typedef int (*fn_remote_del)(void *self, char *file);
typedef int (*fn_remote_add)(void *self, char *file);
typedef int (*fn_error)(char *string);
typedef void (*fn_shutdown)(void *self);

/* External functions */
int monitor_watch(void *self, int poll_interval);
int monitor_monitor(void *self, int poll_interval);
int monitor_watch_add(void *self, const char *path);
int monitor_callback_set(void *self, int type, callback);
void monitor_shutdown(void *self);
int monitor_init(void *self, char *);
int error(char *);

typedef struct monitor_t monitor_t;
struct monitor_t {
	monitor_t *self;
	int sock;
	BIO *bio;
	char *hostname;
	char *username;
	char *password;
	char *directories[DIRS_MAX];
	int _d_idx, _w_pos;
	int parallel_max;
	char *state_file;
	bool initialized;
	file_t *list_now;
	file_t *list_prev;
	/* Functions */
	fn_init init;
	fn_watch_add watch_add;	
	fn_watch watch;
	fn_watch monitor;
	fn_callback_set callback_set;
	fn_error error;
	fn_authenticate authenticate;
	fn_remote_add remote_add;
	fn_remote_del remote_del;
        fn_shutdown shutdown;
	callback add_callback;
	callback del_callback;
	callback mod_callback;	
};

monitor_t *monitor_new(void);

#endif
