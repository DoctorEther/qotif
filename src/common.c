/*
 * common.c - utilities shared by every module of Qotif
 */
#include "common.h"

#include <ctype.h>
#include <locale.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0755)
#endif

static log_hook_fn log_hook;

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "qotif: out of memory\n");
        abort();
    }
    return p;
}

void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) {
        fprintf(stderr, "qotif: out of memory\n");
        abort();
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "qotif: out of memory\n");
        abort();
    }
    return p;
}

char *xstrdup(const char *s)
{
    size_t n;
    char *d;

    if (!s)
        s = "";
    n = strlen(s) + 1;
    d = xmalloc(n);
    memcpy(d, s, n);
    return d;
}

void str_copy(char *dst, const char *src, size_t size)
{
    size_t n;

    if (!size)
        return;
    if (!src)
        src = "";
    n = strlen(src);
    if (n >= size)
        n = size - 1;
    memmove(dst, src, n);
    dst[n] = 0;
}

int str_icmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int str_ieq(const char *a, const char *b)
{
    if (!a || !b)
        return a == b;
    return str_icmp(a, b) == 0;
}

int str_iprefix(const char *s, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

int str_icontains(const char *hay, const char *needle)
{
    size_t n = strlen(needle), i;

    if (!n)
        return 1;
    for (; *hay; hay++) {
        for (i = 0; i < n && hay[i]; i++)
            if (tolower((unsigned char)hay[i]) != tolower((unsigned char)needle[i]))
                break;
        if (i == n)
            return 1;
    }
    return 0;
}

char *str_trim(char *s)
{
    char *e;

    while (*s && isspace((unsigned char)*s))
        s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

void str_lower(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

int str_split(char *s, char sep, char **out, int max)
{
    int n = 0;
    char *p = s;

    while (n < max && p) {
        char *q = strchr(p, sep);
        if (q)
            *q = 0;
        p = str_trim(p);
        if (*p)
            out[n++] = p;
        p = q ? q + 1 : NULL;
    }
    return n;
}

char *file_read_all(const char *path, size_t *len)
{
    FILE *f;
    long size;
    char *data;

    if (len)
        *len = 0;
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    data = xmalloc((size_t)size + 1);
    if (size && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[size] = 0;
    if (len)
        *len = (size_t)size;
    return data;
}

int file_write_all(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (!f)
        return 0;
    ok = fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0)
        ok = 0;
    return ok;
}

int file_exists(const char *path)
{
    struct stat st;
    return path && *path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int dir_exists(const char *path)
{
    struct stat st;
    return path && *path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int dir_make_path(const char *path)
{
    char tmp[PATH_LEN];
    char *p;

    str_copy(tmp, path, sizeof(tmp));
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (!dir_exists(tmp))
                MKDIR(tmp);
            *p = '/';
        }
    }
    if (!dir_exists(tmp))
        MKDIR(tmp);
    return dir_exists(path);
}

int path_is_absolute(const char *path)
{
    return path[0] == '/';
}

void path_join(char *out, size_t size, const char *a, const char *b)
{
    char tmp[PATH_LEN];
    size_t n;

    if (!a || !*a || path_is_absolute(b)) {
        str_copy(out, b, size);
        return;
    }
    n = strlen(a);
    snprintf(tmp, sizeof(tmp), "%s%s%s", a, (a[n - 1] == '/') ? "" : "/", b);
    str_copy(out, tmp, size);
}

void path_dirname(char *out, size_t size, const char *path)
{
    char tmp[PATH_LEN];
    char *slash;

    str_copy(tmp, path, sizeof(tmp));
    slash = strrchr(tmp, '/');
    if (!slash)
        str_copy(out, ".", size);
    else if (slash == tmp)
        str_copy(out, "/", size);
    else {
        *slash = 0;
        str_copy(out, tmp, size);
    }
}

const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

const char *path_ext(const char *path)
{
    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    return dot ? dot : "";
}

void path_strip_ext(char *out, size_t size, const char *path)
{
    char tmp[PATH_LEN];
    char *dot;

    str_copy(tmp, path, sizeof(tmp));
    dot = strrchr(tmp, '.');
    if (dot && dot > strrchr(tmp, '/'))
        *dot = 0;
    str_copy(out, tmp, size);
}

void sb_init(strbuf_t *sb)
{
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

void sb_free(strbuf_t *sb)
{
    free(sb->data);
    sb_init(sb);
}

void sb_appendn(strbuf_t *sb, const char *s, size_t n)
{
    if (sb->len + n + 1 > sb->cap) {
        size_t cap = sb->cap ? sb->cap : 256;
        while (cap < sb->len + n + 1)
            cap *= 2;
        sb->data = xrealloc(sb->data, cap);
        sb->cap = cap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = 0;
}

void sb_append(strbuf_t *sb, const char *s)
{
    sb_appendn(sb, s, strlen(s));
}

void sb_appendf(strbuf_t *sb, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof(buf)) {
        sb_appendn(sb, buf, (size_t)n);
    } else {
        char *big = xmalloc((size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        sb_appendn(sb, big, (size_t)n);
        free(big);
    }
}

char *sb_steal(strbuf_t *sb)
{
    char *d = sb->data ? sb->data : xstrdup("");
    sb_init(sb);
    return d;
}

void log_set_hook(log_hook_fn fn)
{
    log_hook = fn;
}

void log_msg(int level, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    static const char *prefix[] = { "", "warning: ", "error: " };

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "qotif: %s%s\n", prefix[level], buf);
    if (log_hook)
        log_hook(level, buf);
}

/* printf uses the locale's decimal separator (',' in pt_BR...), but numbers
 * written to files must always use '.' */
void fmt_dot_decimal(char *s)
{
    const char *dp = localeconv()->decimal_point;

    if (!dp || !dp[0] || dp[1] || dp[0] == '.')
        return;
    for (; *s; s++)
        if (*s == dp[0])
            *s = '.';
}

void fmt_num(char *buf, size_t size, double v)
{
    double r = floor(v + 0.5);
    size_t n;

    if (fabs(v - r) < 1e-6) {
        snprintf(buf, size, "%.0f", r == 0.0 ? 0.0 : r);
        return;
    }
    snprintf(buf, size, "%.6f", v);
    fmt_dot_decimal(buf);
    n = strlen(buf);
    while (n > 0 && buf[n - 1] == '0')
        buf[--n] = 0;
    if (n > 0 && buf[n - 1] == '.')
        buf[--n] = 0;
}
