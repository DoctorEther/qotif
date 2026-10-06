/*
 * common.h - utilities shared by every module of Qotif
 */
#ifndef QOTIF_COMMON_H
#define QOTIF_COMMON_H

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QOTIF_NAME    "Qotif"
#define QOTIF_VERSION "0.1.0"

#define PATH_LEN 1024

#ifdef __GNUC__
#define PRINTF_LIKE(a, b) __attribute__((format(printf, a, b)))
#else
#define PRINTF_LIKE(a, b)
#endif

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* memory */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* strings */
void str_copy(char *dst, const char *src, size_t size);
int str_icmp(const char *a, const char *b);
int str_ieq(const char *a, const char *b);
int str_iprefix(const char *s, const char *prefix);
int str_icontains(const char *hay, const char *needle);
char *str_trim(char *s);
void str_lower(char *s);
void str_replace_char(char *s, char from, char to);
int str_split(char *s, char sep, char **out, int max);

/* files and paths */
char *file_read_all(const char *path, size_t *len);
int file_write_all(const char *path, const char *data, size_t len);
int file_exists(const char *path);
int dir_exists(const char *path);
int dir_make_path(const char *path);
void path_join(char *out, size_t size, const char *a, const char *b);
void path_dirname(char *out, size_t size, const char *path);
const char *path_basename(const char *path);
const char *path_ext(const char *path);
void path_strip_ext(char *out, size_t size, const char *path);
int path_is_absolute(const char *path);

/* growable string buffer */
typedef struct strbuf_s {
    char *data;
    size_t len, cap;
} strbuf_t;

void sb_init(strbuf_t *sb);
void sb_free(strbuf_t *sb);
void sb_appendn(strbuf_t *sb, const char *s, size_t n);
void sb_append(strbuf_t *sb, const char *s);
void sb_appendf(strbuf_t *sb, const char *fmt, ...) PRINTF_LIKE(2, 3);
char *sb_steal(strbuf_t *sb);

/* logging; the UI installs a hook to mirror messages in its console */
enum { LOG_INFO, LOG_WARN, LOG_ERROR };
typedef void (*log_hook_fn)(int level, const char *msg);
void log_set_hook(log_hook_fn fn);
void log_msg(int level, const char *fmt, ...) PRINTF_LIKE(2, 3);
#define log_info(...)  log_msg(LOG_INFO, __VA_ARGS__)
#define log_warn(...)  log_msg(LOG_WARN, __VA_ARGS__)
#define log_error(...) log_msg(LOG_ERROR, __VA_ARGS__)

/* formats a coordinate the way map files expect it: integers without
 * decimals, other values with up to six decimals */
void fmt_num(char *buf, size_t size, double v);

#endif
