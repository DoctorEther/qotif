/*
 * ini.h - "[section]  key = value" configuration files
 *
 * Keys may repeat inside a section (compile profiles use this), so the
 * file is kept as an ordered list of entries.
 */
#ifndef QOTIF_INI_H
#define QOTIF_INI_H

typedef struct ini_entry_s {
    char *section;
    char *key;
    char *value;
} ini_entry_t;

typedef struct ini_s {
    ini_entry_t *e;
    int n, cap;
} ini_t;

void ini_init(ini_t *ini);
void ini_free(ini_t *ini);
int ini_load(ini_t *ini, const char *path);
int ini_save(const ini_t *ini, const char *path);

const char *ini_get(const ini_t *ini, const char *section, const char *key, const char *def);
int ini_get_int(const ini_t *ini, const char *section, const char *key, int def);
double ini_get_double(const ini_t *ini, const char *section, const char *key, double def);
int ini_get_bool(const ini_t *ini, const char *section, const char *key, int def);

void ini_set(ini_t *ini, const char *section, const char *key, const char *value);
void ini_set_int(ini_t *ini, const char *section, const char *key, int value);
void ini_set_double(ini_t *ini, const char *section, const char *key, double value);
void ini_add(ini_t *ini, const char *section, const char *key, const char *value);
void ini_remove_section(ini_t *ini, const char *section);

/* returns the index of the next entry at or after start that matches the
 * section (and key when not NULL), or -1 */
int ini_next(const ini_t *ini, int start, const char *section, const char *key);

#endif
