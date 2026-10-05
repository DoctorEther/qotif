/*
 * eclass.h - entity definitions (Hammer .fgd and Radiant .def files)
 */
#ifndef QOTIF_ECLASS_H
#define QOTIF_ECLASS_H

#include "map.h"

enum { EC_POINT, EC_SOLID, EC_BASE };

typedef struct eclass_choice_s {
    char value[32];
    char desc[96];
    int def;
} eclass_choice_t;

typedef struct eclass_prop_s {
    char name[64];
    char type[32];       /* string, integer, choices, flags, color255, ... */
    char desc[128];
    char def[128];
    char *help;
    eclass_choice_t *choices;
    int nchoices;
} eclass_prop_t;

typedef struct eclass_s {
    char name[64];
    int kind;
    float color[3];
    int has_color;
    vec3_t mins, maxs;
    int has_size;
    char *desc;
    char **bases;
    int nbases;
    eclass_prop_t *props;
    int nprops;
    int resolved;
} eclass_t;

void eclass_clear(void);
int eclass_load(const char *path);
eclass_t *eclass_find(const char *name);
int eclass_count(void);
eclass_t *eclass_get(int index);
const eclass_prop_t *eclass_find_prop(const eclass_t *ec, const char *key);

/* sets e->ec, using a fallback class for unknown classnames */
void eclass_bind(entity_t *e);
void eclass_bind_map(map_t *m);
void ent_bounds(const entity_t *e, vec3_t mins, vec3_t maxs);
void ent_color(const entity_t *e, float out[3]);

#endif
