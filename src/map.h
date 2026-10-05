/*
 * map.h - entities, brushes and faces; reading and writing .map files
 *
 * Supports the classic Quake "standard" format and the Valve 220 format
 * (texture axes stored per face).
 */
#ifndef QOTIF_MAP_H
#define QOTIF_MAP_H

#include "common.h"
#include "mathlib.h"

struct eclass_s;

typedef struct epair_s {
    char *key;
    char *value;
} epair_t;

typedef struct face_s {
    vec3_t pts[3];       /* the three plane points written to the file */
    plane_t plane;
    int planeok;
    char tex[64];
    double shift[2];
    double rotate;
    double scale[2];
    vec3_t uaxis, vaxis; /* Valve 220 texture axes */
    int valve;
    char *extra;         /* trailing tokens kept verbatim (e.g. Quake 2 flags) */
    winding_t *w;        /* polygon computed by brush_build() */
    int selected;
} face_t;

typedef struct brush_s {
    face_t *faces;
    int nfaces, maxfaces;
    vec3_t mins, maxs;
    int valid;
    int selected;
    struct entity_s *owner;
    struct brush_s *prev, *next;
} brush_t;

typedef struct entity_s {
    epair_t *pairs;
    int npairs, maxpairs;
    brush_t *brushes, *btail;
    int nbrushes;
    vec3_t origin;
    int selected;          /* point entities only */
    struct eclass_s *ec;
    struct entity_s *prev, *next;
} entity_t;

typedef struct map_s {
    entity_t *entities, *etail;
    entity_t *world;
    int valve220;
    int dirty;
    char path[PATH_LEN];
} map_t;

/* name written in the "// Game:" header */
extern char map_game_name[64];

map_t *map_create(void);
void map_free(map_t *m);
void map_add_entity(map_t *m, entity_t *e);
void map_unlink_entity(map_t *m, entity_t *e);
int map_count_brushes(const map_t *m);
int map_count_entities(const map_t *m);
void map_build_all(map_t *m);

entity_t *entity_new(void);
void entity_free(entity_t *e);
const char *ent_get(const entity_t *e, const char *key);
void ent_set(entity_t *e, const char *key, const char *value);
void ent_remove(entity_t *e, const char *key);
const char *ent_classname(const entity_t *e);
int ent_is_world(const entity_t *e);
int ent_is_point(const entity_t *e);
void ent_set_origin(entity_t *e, const vec3_t o);
void ent_add_brush(entity_t *e, brush_t *b);
void ent_unlink_brush(entity_t *e, brush_t *b);

brush_t *brush_new(void);
void brush_free(brush_t *b);
brush_t *brush_copy(const brush_t *b);
void brush_copy_faces(brush_t *dst, const brush_t *src);
face_t *brush_add_face(brush_t *b);
void brush_remove_face(brush_t *b, int index);
void face_copy(face_t *dst, const face_t *src);
void face_free_data(face_t *f);

map_t *map_parse(const char *text, char *err, size_t errsize);
map_t *map_load(const char *path, char *err, size_t errsize);
char *map_write_string(const map_t *m, int selected_only);
int map_save(const map_t *m, const char *path);

#endif
