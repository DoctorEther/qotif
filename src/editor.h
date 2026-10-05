/*
 * editor.h - editor state, selection and editing commands
 *
 * Everything here is independent of the toolkit; the Motif front end
 * lives in ui_*.c and is reached through ui.h.
 */
#ifndef QOTIF_EDITOR_H
#define QOTIF_EDITOR_H

#include "brush.h"
#include "eclass.h"
#include "map.h"

typedef enum {
    TOOL_SELECT, TOOL_CAMERA, TOOL_ENTITY, TOOL_BLOCK, TOOL_TEXTURE, TOOL_CLIP, TOOL_VERTEX,
    TOOL_COUNT
} tool_t;

enum { HANDLE_SCALE, HANDLE_ROTATE };
enum { CLIP_BOTH, CLIP_FRONT, CLIP_BACK };
enum { SEL_SET, SEL_ADD, SEL_TOGGLE };

typedef struct camera_s {
    vec3_t pos;
    double yaw, pitch;   /* degrees; pitch > 0 looks up */
} camera_t;

typedef struct editor_s {
    map_t *map;
    tool_t tool;
    int grid;
    int snap;
    int show_grid;
    int texlock;
    int ignore_groups;
    char texture[64];
    char entclass[64];
    int prim;
    int prim_sides;
    camera_t cam;
    int handles;

    /* block tool */
    int preview;
    vec3_t preview_mins, preview_maxs;
    int have_last_block;
    vec3_t last_block_mins, last_block_maxs;

    /* clip tool */
    vec3_t clip_pts[2];
    int clip_npts;
    int clip_axis;
    int clip_mode;

    /* vertex tool: positions of the selected vertices */
    vec3_t *vsel;
    int nvsel;

    /* leak trail loaded from a .pts/.lin file */
    vec3_t *pointfile;
    int npointfile;

    char *clipboard;
} editor_t;

extern editor_t ed;

typedef struct pick_s {
    entity_t *ent;
    brush_t *brush;     /* NULL for point entities */
    int face;
    double t;
    vec3_t point;
} pick_t;

void ed_init(void);
void ed_new_map(void);
int ed_load_map(const char *path);
int ed_save_map(const char *path);
void ed_set_map(map_t *m);
void ed_load_resources(void);
void ed_set_tool(tool_t t);
void ed_set_grid(int grid);
void ed_mark_dirty(void);
const char *ed_default_texture(void);
int ed_load_pointfile(const char *path);
void ed_clear_pointfile(void);

/* selection */
int sel_count(void);
int sel_brush_count(void);
int sel_bounds(vec3_t mins, vec3_t maxs);
void sel_clear(void);
void sel_brush(brush_t *b, int mode);
void sel_entity(entity_t *e, int mode);
void sel_pick(const pick_t *p, int mode);
int pick_is_selected(const pick_t *p);
void sel_all(void);
void sel_invert(void);
void sel_box(int hax, int vax, double h0, double v0, double h1, double v1);
int sel_entities(entity_t ***out);
void sel_duplicate_in_place(void);
void sel_faces_clear(void);
int sel_face_count(void);
face_t *sel_first_face(brush_t **owner);
void sel_changed(void);

/* interactive transforms: originals are kept so each step starts fresh */
void tr_begin(void);
void tr_apply(mat3_t m, const vec3_t t);
void tr_end(int commit);
int tr_active(void);

/* commands (each records its own undo step) */
void cmd_delete(void);
void cmd_copy(void);
void cmd_cut(void);
void cmd_paste(void);
void cmd_duplicate(int offset);
void cmd_translate(const vec3_t d);
void cmd_flip(int axis);
void cmd_rotate(int axis, double degrees);
void cmd_snap_selection(void);
void cmd_tie_to_entity(const char *classname);
void cmd_move_to_world(void);
void cmd_carve(void);
void cmd_hollow(double thickness);
void cmd_merge(void);
void cmd_clip_apply(void);
void cmd_apply_texture(void);
void cmd_create_brush(const vec3_t mins, const vec3_t maxs, int hax, int vax, int dax);
entity_t *cmd_create_entity(const char *classname, const vec3_t origin);
void cmd_convert_valve(void);
int cmd_goto_brush(int entity_index, int brush_index);
void cmd_map_info(char *buf, size_t size);
void cmd_check_map(void);

/* picking */
int pick_ray(const vec3_t org, const vec3_t dir, pick_t *out);
int pick_2d(int hax, int vax, int dax, double h, double v, pick_t *out);
int clip_plane(plane_t *pl);

/*
 * vertex tool: handles on the vertices, edge midpoints and face centers of
 * the selected brushes.  A handle controls a set of vertex positions (one
 * for a vertex, two for an edge, all of a face's for a face); dragging moves
 * the selected positions and rebuilds each brush as the convex hull of its
 * moved vertices, keeping the texturing of the faces.
 */
enum { VH_VERTEX, VH_EDGE, VH_FACE };

typedef struct vhandle_s {
    int kind;
    vec3_t pos;
    int first, count;   /* range in the vertex array returned with it */
} vhandle_t;

int vtx_collect(vhandle_t **handles, vec3_t **verts);
int vtx_handle_selected(const vhandle_t *h, const vec3_t *verts);
/* mode: SEL_SET replaces the selection, SEL_TOGGLE adds/removes */
void vtx_select(const vhandle_t *hs, const int *which, int n, const vec3_t *verts, int mode);
void vtx_clear(void);
void vtx_drag_begin(void);
int vtx_drag_apply(const vec3_t delta);
void vtx_drag_end(int commit, const vec3_t delta);

/* undo.c */
void undo_clear(void);
void undo_push(const char *desc);
int undo_undo(void);
int undo_redo(void);
const char *undo_desc(void);
const char *redo_desc(void);

#endif
