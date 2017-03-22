#include "monitor.h"
#include "network.h"
#include "system.h"
#include <sys/wait.h>
#include <ctype.h>

bool first_run = false;
bool quit = false;

char *_get_state_file_name(const char *path, const char *hostname, const char *username);
file_t *file_list_state_get(const char *path);

int error(char *str)
{
	fprintf(stdout, "Error: %s\n", str);
	exit(1 << 7);
}

static int n_jobs = 0;

int
wait_for_job(void)
{
        int status; 
	pid_t pid = wait(&status);
	if (pid <= 0)
		error("wait");	
        
        if (!WIFEXITED(status)) return (1); 
        status = WEXITSTATUS(status);
  
	--n_jobs;

        return (status);
}

int
wait_for_all_jobs(void)
{
	while (n_jobs) {
		int error = wait_for_job();
                if (error)
			return (0);
	}

        return (1);
}

monitor_t *
monitor_new(void)
{
	monitor_t *mon = calloc(1, sizeof(monitor_t));
	if (!mon)
		return (NULL);

	mon->error = &error;
	mon->callback_set = &monitor_callback_set;
	mon->watch_add = &monitor_watch_add;
	mon->init = &monitor_init;
	mon->watch = &monitor_watch;
	mon->mainloop = &monitor_mainloop;
	mon->self = mon;
	mon->authenticate = &authenticate;
	mon->remote_add = remote_file_add;
	mon->remote_del = remote_file_del;
        mon->shutdown = &monitor_shutdown;

	return (mon);
}

int
monitor_mainloop(void *self, int interval) 
{
	monitor_t *mon = self;
	if (mon->_d_idx == 0) exit(1 << 0);

	mon->list_prev = file_list_state_get(mon->state_file);
	if (!mon->list_prev) {
		first_run = true; // initialise!!!
		mon->list_prev = monitor_files_get(self, mon->list_prev);	
	}

	// this can run and monitor in semi-realtime
	// if interval is > 0 then program will scan
	// for changes every interval seconds...
        while (monitor_watch(self, interval) && !quit);

	return (1);
}                

void
_clear_password(char *pass) 
{
        while (*pass) {
                *pass = '\0';
                pass++;
        }
}

void
monitor_shutdown(void *self)
{
        monitor_t *mon = self;

        _clear_password(mon->password);
        if (mon->username)
                free(mon->username);
        if (mon->password)
                free(mon->password);
        if (mon->hostname)
                free(mon->hostname);

        free(mon);
}

int
monitor_callback_set(void *self, int type, callback func)
{
	monitor_t *mon = self;

        switch (type) {
        case MONITOR_ADD:
        mon->add_callback = func;
        break;
        case MONITOR_DEL:
        mon->del_callback = func;
        break;
        case MONITOR_MOD:
        mon->mod_callback = func;
        };

	return (1);
}

void 
file_list_free(file_t *list)
{
	file_t *c = list;

	while (c) {
		file_t *next = c->next;
		free(c->path);
		free(c);
		c = next;
	}
}

file_t * 
file_list_add(file_t *list, char *path, struct stat *st)
{
	file_t *c = list;

	while (c->next)
		c = c->next;

	if (! c->next) {
		c->next = calloc(1, sizeof(file_t));
		if (!c->next)
			error("calloc()");
		c = c->next;
		c->next = NULL;
		c->mtime = st->st_mtime;
		c->size = st->st_size;
		c->path = strdup(path);
		c->changed = 0; 
	}

	return (list);
}

file_t *
file_exists(file_t *list, const char *filename)
{
	file_t *f = list->next;

	while (f) {
		if (!strcmp(f->path, filename)) {
			return (f);
		}
		f = f->next;
	}
	
	return (NULL);
}

int
_check_add_files(monitor_t *mon, file_t *first, file_t *second)
{
	file_t *f = second->next;
	int changes = 0;

	while (f) {
		file_t *exists = file_exists(first, f->path);
		if ((!exists) || (first_run)) {
			if (mon->add_callback)
				mon->add_callback(f);

			f->changed = MONITOR_ADD;
			if (n_jobs == mon->cpu_count) {
				wait_for_job();
			}
			pid_t pid = fork();
			if (pid < 0)
				error("fork");
			else if (pid == 0) {
			        int status = mon->remote_add(mon->self, f->path);
				exit(status);
			}
			++n_jobs;
			if (!first_run)
				printf("add file : %s\n", f->path);
			else
				printf("init file : %s\n", f->path);
			++changes;
		}
		f = f->next;
	}

	return (changes);
}

int
_check_del_files(monitor_t *mon, file_t *first, file_t *second)
{
	file_t *f = first->next;
	int changes = 0;

	while (f) {
		file_t *exists = file_exists(second, f->path);
		if (!exists) {
			f->changed = MONITOR_DEL;
			if (mon->del_callback) 
				mon->del_callback(f);
                        if (n_jobs == mon->cpu_count) {
                                wait_for_job();
                        }
                        pid_t pid = fork();
                        if (pid < 0)
                                error("fork");
                        else if (pid == 0) {
                                int status = mon->remote_del(mon->self, f->path);
                                exit(status);
                        }
                        ++n_jobs;
			printf("del file : %s\n", f->path);
			changes++;
		}
		f = f->next;
	}

	return (changes);
}

int
_check_mod_files(monitor_t* mon, file_t *first, file_t *second)
{
	file_t *f = second->next;
	int changes = 0;

	while (f) {
		file_t *exists = file_exists(first, f->path);
		if (exists) {
			if (f->mtime != exists->mtime) {
				f->changed = MONITOR_MOD;
				if (mon->mod_callback)
					mon->mod_callback(f);

				if (n_jobs == mon->cpu_count) {
					wait_for_job();
				}
				pid_t pid = fork();
				if (pid < 0)
					error("fork");
				else if (pid == 0) {
					int status = mon->remote_add(mon->self, f->path);
					exit(status);
				}
				++n_jobs;
				printf("mod file : %s\n", f->path);
				changes++;
			}
		}
		f = f->next;
	}
	
	return (changes);
}

void
_transfer_error(monitor_t *mon)
{
	monitor_shutdown(mon);
        fprintf(stderr, "FATAL: transfer error. Test network and retry!\n");
        exit(1);
}

int
file_lists_compare(monitor_t *monitor, file_t *first, file_t *second)
{
        int success = 0;
	int modifications = 0;
	int total = 0;

	// this ordering is important 
	// we are using concurrency here...so...
	// don't mix change types...
	
	modifications = _check_add_files(monitor, first, second);
        if (modifications) {
		total += modifications;
		success = wait_for_all_jobs(); 
                if (!success) {
                       _transfer_error(monitor);
		}
	}

	modifications = _check_mod_files(monitor, first, second);
        if (modifications) {
		total += modifications;
		success = wait_for_all_jobs();	
                if (!success) {
                        _transfer_error(monitor);
		}
	}
	
	modifications = _check_del_files(monitor, first, second);
        if (modifications) {
		total += modifications;
		success = wait_for_all_jobs();	
                if (!success) {
                        _transfer_error(monitor);
                }
	}

	if (total) {
		printf("total of %d actions\n", total);
	}

	return (total);
}

const char *
directory_next(monitor_t *mon)
{
	if (mon->directories[mon->_w_pos] == NULL) {
		mon->_w_pos = 0; 
		return (NULL);
	}

	return (mon->directories[mon->_w_pos++]);
}

void
 _list_append(file_t *one, file_t *two)
{
	file_t *c = one;
	while (c->next)
		c = c->next;

	if (two->next)
		c->next = two->next;
	else
		c->next = NULL;
}

file_t * 
scan_recursive(const char *path)
{
	DIR *dir = opendir(path);
	if (!dir) return (NULL);
	struct dirent *dh = NULL;
	char *directories[8192] = { NULL };
	int i; 

	file_t *list = calloc(1, sizeof(file_t));
	if (!list)
		error("calloc()");

	list->next = NULL;

	for (i = 0; i < sizeof(directories) / sizeof(char *); i++) {
		directories[i] = NULL;
	}

	i = 0;

	while ((dh = readdir(dir)) != NULL) {
		if (dh->d_name[0] == '.') continue;
		
		char buf[PATH_MAX];
		snprintf(buf, sizeof(buf), "%s%c%s", path, SLASH, dh->d_name);
		struct stat fstat;
		if (stat(buf, &fstat) < 0) continue;

                if (S_ISLNK(fstat.st_mode)) continue;

		if (S_ISDIR(fstat.st_mode)) {
			directories[i++] = strdup(buf);			
			continue;
		} else {
			list = file_list_add(list, buf, &fstat);
		}
	}

	closedir(dir);

	i = 0;

	/* recursive scan */

	while (directories[i] != NULL) {
		file_t *next = NULL;
		next = scan_recursive(directories[i]);
		_list_append(list, next);
		free(directories[i++]);
	} 

	return (list);
}

/* It's perfectly okay to monitor more than 1 directory */
file_t *
monitor_files_get(monitor_t *mon, file_t *list)
{
	const char *path;

	while ((path = directory_next(mon)) != NULL) {
		list = scan_recursive(path);
	}

	return (list);
}

char *
_get_state_file_name(const char *path, const char *hostname, const char *username)
{
	char buf[PATH_MAX];
	char absolute[PATH_MAX * 2 + 1];
	realpath(path, absolute);
#if defined(__linux__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__)
	const char *home = getenv("HOME");
#else
	const char *home = getenv("HOMEPATH");
#endif
	
	snprintf(buf, sizeof(buf), "%s/.%s", home, PROGRAM_NAME);
	struct stat st;

	// make directory to store state file lists...
	if (stat(buf, &st) < 0)
		mkdir(buf, 0755);

	char hashname[(strlen(username) * 2) + (strlen(hostname) * 2) + (strlen(absolute) * 2) + 1];
 
	char hashvalue[PATH_MAX * 2 + 1];     
        snprintf(hashvalue, sizeof(hashvalue), "%s:%s:%s", username, hostname, absolute);	

	memset(hashname, 0, strlen(hashvalue) * 2 + 1);	
	for (int i = 0; i < strlen(hashvalue); i++)
		snprintf(hashname, sizeof(hashname), "%s%2x", hashname, hashvalue[i]);

	// return path for unique state file path (unique to user, location and destination)
	snprintf(buf, sizeof(buf), "%s/%s", buf, hashname);
	return (strdup(buf));
}

file_t *
file_list_state_get(const char *path)
{
	char buf[4096];
	FILE *f = fopen(path, "r");
	if (!f) return (NULL);

	file_t *list = calloc(1, sizeof(file_t));

	while ((fgets(buf, sizeof(buf), f)) != NULL) {
		struct stat fstat;
                if (buf[0] == '#') continue;

		char *path_start = buf;
		char *path_end = strchr(path_start, '\t');
		if (!path_end) continue;
		*path_end = '\0';

		char *mtime_start = path_end + 1;
		char *mtime_end = strchr(mtime_start, '\t');
		if (!mtime_end) continue;
		*mtime_end = '\0';

		char *size_start = mtime_end + 1;
		if (!size_start) continue;


		fstat.st_size = atoi(size_start);
		fstat.st_mtime = atoi(mtime_start);

		list = file_list_add(list, path_start, &fstat);
	}

	fclose(f);

	return (list); 
}

void
file_list_state_save(const char *path, file_t *current_files)
{
	file_t *cursor = current_files->next;
	FILE *f = fopen(path, "w");
	if (!f) exit(1 << 4);

	while (cursor) {
		fprintf(f, "%s\t%d\t%d\n", cursor->path, cursor->mtime, cursor->size);
		cursor = cursor->next;
	}

	fclose(f);
}

file_t *
_monitor_compare_lists(void *self, file_t *one, file_t *two)
{
	monitor_t *m = self;
	int changes = file_lists_compare(m, one, two);
	file_list_free(one);
	one = two;
        
	if (changes) {
		file_list_state_save(m->state_file, one);
	}

	return (one);
}

int 
monitor_watch(void *self, int poll)
{
	monitor_t *mon = self;

	if (!mon->initialized) return (0);

	mon->list_now = monitor_files_get(self, mon->list_now);
	mon->list_prev = _monitor_compare_lists(self, mon->list_prev, mon->list_now);  
       
        if (poll) {
		sleep(poll); 
		return (1);
	} 

	quit = true;
	
	return (0);
}

/* It's acceptable and fine to monitor > 1 directory */
/* if you want to anyway... */
int
monitor_watch_add(void *self, const char *path)
{
	monitor_t *mon = self;
	if (mon->_d_idx >= DIRS_MAX) 
		error("watch_add(): dirs limit reached!");	

	struct stat dstat;
	if (stat(path, &dstat) < 0)
		error("does directory exist? check permissions.");
	
	if (!S_ISDIR(dstat.st_mode))
		error("watch_add(): not a directory.");
	
	mon->directories[mon->_d_idx++] = strdup(path);

	mon->state_file = _get_state_file_name(path, mon->hostname, mon->username);

	return (1);
}

void
exit_safe(int sig)
{
        if (sig != SIGINT && sig != SIGTERM) return;
        quit = true;
}

int
set_arguments(monitor_t *mon, char *cmd_string)
{
        char buf[PATH_MAX];
        char *user_start = cmd_string;
        char *user_end = strchr(user_start, '@');
        if (!user_end) return (0);
        *user_end = '\0';

        mon->username = strdup(user_start);

        char *host_start = user_end + 1;
        if (!host_start) return (0);
        char *host_end = strchr(host_start, ':');
        if (!host_end) return (0);
        *host_end = '\0';

        mon->hostname = strdup(host_start);

        char *directory = host_end + 1;
        realpath(directory, buf);

        mon->watch_add(mon->self, buf);
        mon->cpu_count = system_cpu_count();

	return (1);
}


int
monitor_init(void *self, char *cmd_string)
{
	monitor_t *mon = self;

	if (!set_arguments(mon, cmd_string)) {
		return (0);
	}
	
	mon->directories[mon->_d_idx] = NULL;
	mon->directories[DIRS_MAX - 1] = NULL;

	signal(SIGINT, exit_safe);
	signal(SIGTERM, exit_safe);

	mon->list_prev = NULL;
	mon->list_now = NULL;	
	mon->initialized = true;

	return (1);
}

