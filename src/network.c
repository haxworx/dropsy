#include "network.h"
#include "monitor.h"

#include <stdarg.h>
#include <unistd.h>

#define REMOTE_URI "/any"
#define BUF_MAX 4096

void Error(char *fmt, ...)
{
	char mesg[BUF_MAX];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(mesg, sizeof(mesg), fmt, ap);
	fprintf(stderr, "Error: %s\n", mesg);
	va_end(ap);

	exit(1);
}

char *
_file_from_path(char *path)
{
	if (!path) return NULL;

	char *t = strrchr(path, '/');
	if (t) {
		t++;
		return t;
	}
	return path;
}

char *
_directory_from_path(char *path)
{
	int i;
	char buf[PATH_MAX + 1];
	char *cwd = getcwd(buf, PATH_MAX + 1);

	if (!path) return NULL;
	
	// /home/netstar/files becomes files/	
	for (i = 0; cwd[i] == path[i]; i++);

	// /etc becomes etc	
	while (path[i] == '/')
		i++;

	char *path_begin = &path[i];
	char *t = strrchr(path_begin, '/');
	if (t) {
		*t = '\0';
		return path_begin;
	}
	return path_begin;
}

int
_status_code(char *buf)
{
#define AUTH_STATUS_STR "status: "
        int status = 1000;
        char *status_position = strstr(buf, AUTH_STATUS_STR);
        if (status_position) {
                status_position = strchr(status_position, ':');
                ++status_position;
                char *start = status_position;
                while (*status_position != '\r')
                        status_position++;
                if (*status_position == '\r') {
                        *status_position = '\0';
                        status = atoi(start);
                }
        }

        return status;
}        

int
authenticate(void *self)
{
	monitor_t *mon = self;

	char echo_to_screen[BUF_MAX];
	snprintf(echo_to_screen, sizeof(echo_to_screen), "%s@%s's password: ",
			mon->username, mon->hostname);

	char *guess = getpass(echo_to_screen);
	if (!guess || strlen(guess) == 0) exit(1 << 2);

	mon->password = strdup(guess);

	mon->bio = Connect_SSL(mon->hostname, DEFAULT_PORT);
	if (!mon->bio)
		mon->error("Could not connect!");

	char post[BUF_MAX];
	char *fmt =
		"POST %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Username: %s\r\n"
		"Password: %s\r\n"
		"Action: AUTH\r\n"
		"\r\n";

	snprintf(post, sizeof(post), fmt, REMOTE_URI, mon->hostname,
			mon->username, mon->password);

	Write(mon, post, strlen(post));

	char buf[BUF_MAX] = { 0 };
	ssize_t len = 0;

	len = Read(mon, buf, sizeof(buf));
	buf[len] = '\0';

	int status = _status_code(buf);

	Close(mon);

	if (status != 1)
		mon->error("Invalid username or password");

	return 0;
}

int 
remote_file_del(void *self, char *file)
{
	monitor_t *mon = self;
        char path[PATH_MAX] = { 0 };
        snprintf(path, sizeof(path), "%s", file);

        mon->bio = Connect_SSL(mon->hostname, DEFAULT_PORT);
        if (!mon->bio) {
                fprintf(stderr, "Could not connect()\n");
                return 1;
        }

        int content_length = 0;

        char *file_from_path = _file_from_path(path);

        char dirname[PATH_MAX] = { 0 };
        snprintf(dirname, sizeof(dirname), "%s", mon->directories[0]);
        char *dir_from_path = _directory_from_path(path);

        char post[BUF_MAX] = { 0 };
        char *fmt =
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Length: %d\r\n"
            "Username: %s\r\n"
            "Password: %s\r\n"
            "Directory: %s\r\n" "Filename: %s\r\n" "Action: DEL\r\n\r\n";

        snprintf(post, sizeof(post), fmt, REMOTE_URI, mon->hostname,
                 content_length, mon->username, mon->password, dir_from_path,
                 file_from_path);
        Write(mon, post, strlen(post));

        char buf[BUF_MAX];

        int bytes = Read(mon, buf, sizeof(buf));
        if (bytes <= 0) return 1;
        buf[bytes] = '\0';
	
        Close(mon);

        int status = _status_code(buf);
        if (status != 1) return 1;


        return 0;
}

int remote_file_add(void *self, char *file)
{
	monitor_t *mon = self;
        char path[PATH_MAX] = { 0 };
        snprintf(path, sizeof(path), "%s", file);
        char dirname[PATH_MAX] = { 0 };
        snprintf(dirname, sizeof(dirname), "%s", mon->directories[0]);

        mon->bio = Connect_SSL(mon->hostname, DEFAULT_PORT);
        if (!mon->bio) {
                fprintf(stderr, "Could not connect()\n");
                return 1;
        }
        struct stat fstats;
        if (strlen(path) == 0) {
                return 1;
        }

        if (stat(path, &fstats) < 0) {
                return 1;
        }
        FILE *f = fopen(path, "rb");
        if (f == NULL) {
                mon->error
                    ("Unable to open filename this should not happen!");
                     
        }

#define CHUNK 1024

        char buffer[CHUNK + 1] = { 0 };

        int content_length = fstats.st_size;

        char *file_from_path = _file_from_path(path);
        char *dir_from_path = _directory_from_path(path);

        char post[BUF_MAX] = { 0 };
        char *fmt =
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Length: %d\r\n"
            "Username: %s\r\n"
            "Password: %s\r\n"
            "Filename: %s\r\n" "Directory: %s\r\n" "Action: ADD\r\n\r\n";
        snprintf(post, sizeof(post), fmt, REMOTE_URI, mon->hostname,
                 content_length, mon->username, mon->password, file_from_path,
                 dir_from_path);
        Write(mon, post, strlen(post));
        int total = 0;

        int size = content_length;
        while (size) {
                while (1) {
                        int count = fread(buffer, 1, CHUNK, f);
                        //ssize_t bytes = send(sock, buffer, count, 0);
                        // this is exactly the same with 0 flag
                        ssize_t bytes = Write(mon, buffer, count);
                        if (bytes < count) {
                                return 1;
                        }
                        if (bytes == 0) {
                                break;
                        } else if (bytes < 0) {
                                return 1;
                        } else {
                                size -= bytes;
                                total += bytes;
                        }
                }
        }

        if (size != 0) {
                mon->error("wtf?");
        }

        fclose(f);
        
        char buf[BUF_MAX];

        int bytes = Read(mon, buf, sizeof(buf));
        if (bytes <= 0) return 1;
        buf[bytes] = '\0';

        Close(mon);

	int status = _status_code(buf);
        if (status != 1) return 1;

        return 0;
}

int 
Connect(const char *hostname, int port)
{
	int sock;
	struct hostent *host;
	struct sockaddr_in inaddr;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) return 0;

	host = gethostbyname(hostname);
	if (host == NULL) return 0;

	inaddr.sin_family = AF_INET;
	inaddr.sin_port = htons(port);
	inaddr.sin_addr = *((struct in_addr *)host->h_addr_list[0]);
	memset(&inaddr.sin_zero, 0, 8);

	int status = connect(sock, (struct sockaddr *) &inaddr,
				sizeof(struct sockaddr));

	if (status == 0) {
		return sock;
	}

	return 0;
}

BIO *Connect_SSL(char *hostname, int port)
{
        BIO *bio = NULL;
        char bio_addr[BUF_MAX] = { 0 };

        snprintf(bio_addr, sizeof(bio_addr), "%s:%d", hostname, port);

        SSL_library_init();

        SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
        SSL *ssl = NULL;

        bio = BIO_new_ssl_connect(ctx);
        if (bio == NULL) {
                Error("BIO_new_ssl_connect");
        }

        BIO_get_ssl(bio, &ssl);
        SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
        BIO_set_conn_hostname(bio, bio_addr);

        if (BIO_do_connect(bio) <= 0) {
                Error("SSL Unable to connect");
        }

        return bio;
}

ssize_t Read(void *self, char *buf, int len)
{
	monitor_t *mon = self;
	if (mon->bio)
		return BIO_read(mon->bio, buf, len);

	return read(mon->sock, buf, len);
}

ssize_t Write(void *self, char *buf, int len)
{
	monitor_t *mon = self;
	if (mon->bio)
		return BIO_write(mon->bio, buf, len);

	return write(mon->sock, buf, len);
}

int Close(void *self)
{
	monitor_t *mon = self;
	if (mon->bio) {
		BIO_free_all(mon->bio);
		mon->bio = NULL;
		return 0;
	}
	
	return close(mon->sock);
}
