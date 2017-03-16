#ifndef __NETWORK_H__
#define __NETWORK_H__
#include <string.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define DEFAULT_PORT 12345


int authenticate(void *self);
int remote_file_add(void *self, char *file);
int remote_file_del(void *self, char *file);

char *strdup(const char *s);
BIO* Connect_SSL(char *hostname, int port);
int Connect(const char *hostname, int port);
ssize_t Read(void *self, char *buf, int len);
ssize_t Write(void *self, char *buf, int len);
int Close(void *self);
#endif
