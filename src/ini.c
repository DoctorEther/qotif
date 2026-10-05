/*
 * ini.c - "[section]  key = value" configuration files
 */
#include "ini.h"
#include "common.h"

void ini_init(ini_t *ini)
{
    ini->e = NULL;
    ini->n = ini->cap = 0;
}

void ini_free(ini_t *ini)
{
    int i;
    for (i = 0; i < ini->n; i++) {
        free(ini->e[i].section);
        free(ini->e[i].key);
        free(ini->e[i].value);
    }
    free(ini->e);
    ini_init(ini);
}

void ini_add(ini_t *ini, const char *section, const char *key, const char *value)
{
    if (ini->n >= ini->cap) {
        ini->cap = ini->cap ? ini->cap * 2 : 32;
        ini->e = xrealloc(ini->e, sizeof(ini_entry_t) * (size_t)ini->cap);
    }
    ini->e[ini->n].section = xstrdup(section);
    ini->e[ini->n].key = xstrdup(key);
    ini->e[ini->n].value = xstrdup(value);
    ini->n++;
}

int ini_load(ini_t *ini, const char *path)
{
    size_t len;
    char *text = file_read_all(path, &len);
    char section[256] = "";
    char *line, *next;

    if (!text)
        return 0;
    for (line = text; line && *line; line = next) {
        char *s, *eq;

        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        s = str_trim(line);
        if (!*s || *s == ';' || *s == '#')
            continue;
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e)
                *e = 0;
            str_copy(section, str_trim(s + 1), sizeof(section));
            continue;
        }
        eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = 0;
        ini_add(ini, section, str_trim(s), str_trim(eq + 1));
    }
    free(text);
    return 1;
}

int ini_save(const ini_t *ini, const char *path)
{
    strbuf_t sb;
    int i, j, ok;

    sb_init(&sb);
    for (i = 0; i < ini->n; i++) {
        int seen = 0;
        for (j = 0; j < i; j++)
            if (str_ieq(ini->e[j].section, ini->e[i].section)) {
                seen = 1;
                break;
            }
        if (seen)
            continue;
        if (sb.len)
            sb_append(&sb, "\n");
        if (*ini->e[i].section)
            sb_appendf(&sb, "[%s]\n", ini->e[i].section);
        for (j = i; j < ini->n; j++)
            if (str_ieq(ini->e[j].section, ini->e[i].section))
                sb_appendf(&sb, "%s = %s\n", ini->e[j].key, ini->e[j].value);
    }
    ok = file_write_all(path, sb.data ? sb.data : "", sb.len);
    sb_free(&sb);
    return ok;
}

int ini_next(const ini_t *ini, int start, const char *section, const char *key)
{
    int i;
    for (i = start < 0 ? 0 : start; i < ini->n; i++) {
        if (!str_ieq(ini->e[i].section, section))
            continue;
        if (key && !str_ieq(ini->e[i].key, key))
            continue;
        return i;
    }
    return -1;
}

const char *ini_get(const ini_t *ini, const char *section, const char *key, const char *def)
{
    int i = ini_next(ini, 0, section, key);
    return i >= 0 ? ini->e[i].value : def;
}

int ini_get_int(const ini_t *ini, const char *section, const char *key, int def)
{
    const char *v = ini_get(ini, section, key, NULL);
    return (v && *v) ? atoi(v) : def;
}

double ini_get_double(const ini_t *ini, const char *section, const char *key, double def)
{
    const char *v = ini_get(ini, section, key, NULL);
    return (v && *v) ? atof(v) : def;
}

int ini_get_bool(const ini_t *ini, const char *section, const char *key, int def)
{
    const char *v = ini_get(ini, section, key, NULL);
    if (!v || !*v)
        return def;
    return str_ieq(v, "1") || str_ieq(v, "true") || str_ieq(v, "yes") || str_ieq(v, "on");
}

void ini_set(ini_t *ini, const char *section, const char *key, const char *value)
{
    int i = ini_next(ini, 0, section, key);
    if (i >= 0) {
        free(ini->e[i].value);
        ini->e[i].value = xstrdup(value);
        return;
    }
    ini_add(ini, section, key, value);
}

void ini_set_int(ini_t *ini, const char *section, const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    ini_set(ini, section, key, buf);
}

void ini_set_double(ini_t *ini, const char *section, const char *key, double value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    ini_set(ini, section, key, buf);
}

void ini_remove_section(ini_t *ini, const char *section)
{
    int i, o = 0;
    for (i = 0; i < ini->n; i++) {
        if (str_ieq(ini->e[i].section, section)) {
            free(ini->e[i].section);
            free(ini->e[i].key);
            free(ini->e[i].value);
            continue;
        }
        ini->e[o++] = ini->e[i];
    }
    ini->n = o;
}
