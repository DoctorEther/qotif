/*
 * view.h - viewports (3D camera and 2D top/front/side) and mouse handling
 */
#ifndef QOTIF_VIEW_H
#define QOTIF_VIEW_H

#include "editor.h"

typedef enum { VIEW_3D, VIEW_TOP, VIEW_FRONT, VIEW_SIDE, VIEW_TYPE_COUNT } viewtype_t;
typedef enum { RENDER_TEXTURED, RENDER_FLAT, RENDER_WIRE } rendermode_t;

enum { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4 };
enum { BTN_LEFT = 1, BTN_MIDDLE = 2, BTN_RIGHT = 3 };

/* camera fly keys */
enum {
    FLY_FORWARD = 1, FLY_BACK = 2, FLY_LEFT = 4, FLY_RIGHT = 8, FLY_UP = 16, FLY_DOWN = 32
};

/* mouse interaction modes */
enum {
    MODE_NONE, MODE_PAN, MODE_LOOK, MODE_STRAFE, MODE_MOVE, MODE_HANDLE,
    MODE_RUBBER, MODE_BLOCK, MODE_CLIP, MODE_CAMERA, MODE_VERTEX
};

#define MAX_VIEWS 4

typedef struct view_s {
    int index;
    viewtype_t type;
    rendermode_t rmode;
    int width, height;
    int hax, vax, dax;      /* world axes shown horizontally/vertically/depth */
    double cx, cy;          /* 2D: world coordinates at the center */
    double zoom;            /* 2D: pixels per unit */

    /* mouse interaction */
    int mode;
    int button;
    int mods;
    int press_x, press_y;
    int last_x, last_y;
    int mouse_x, mouse_y;
    int moved;
    int click_on_selected;
    int handle_h, handle_v;
    double accum;
    vec3_t start_world;
    vec3_t orig_mins, orig_maxs;
    int clip_drag;
    vec3_t ctx_point;       /* world point of the last context menu */
    vec3_t vref;            /* vertex tool: position of the dragged handle */
    vec3_t vdelta;          /* vertex tool: current offset */

    void *ui;               /* toolkit widget */
} view_t;

extern view_t views[MAX_VIEWS];
extern view_t *active_view;
extern int fly_keys;
/* 3D view whose camera follows the mouse without a button held (Z) */
extern view_t *captured_view;

void view_init(view_t *v, int index, viewtype_t type);
void view_set_type(view_t *v, viewtype_t type);
void view_axes(viewtype_t type, int *h, int *v, int *d);
const char *view_type_name(viewtype_t type);
const char *view_label(const view_t *v);
int view_label_hit(const view_t *v, int x, int y);

void view_to_world(const view_t *v, int x, int y, double *h, double *vv);
void view_to_screen(const view_t *v, double h, double vv, double *x, double *y);
void view_center_on(view_t *v, const vec3_t p);
void view_ray(const view_t *v, int x, int y, vec3_t org, vec3_t dir);
/* screen position of a world point; 0 when it is behind the 3D camera */
int view_project(const view_t *v, const vec3_t p, double *sx, double *sy);
void camera_vectors(const camera_t *c, vec3_t fwd, vec3_t right, vec3_t up);
void camera_look_at(const vec3_t target, double distance);

void view_mouse_down(view_t *v, int button, int x, int y, int mods);
void view_mouse_up(view_t *v, int button, int x, int y, int mods);
void view_mouse_move(view_t *v, int x, int y, int mods);
void view_wheel(view_t *v, int dir, int x, int y, int mods);
void view_mouse_leave(view_t *v);
int view_cancel(void);
void view_set_capture(view_t *v, int on);

void views_center_2d(const vec3_t p);
void fly_step(double dt, int fast);

/* selection handles in 2D views: returns 1 and the handle position */
int view_handle_pos(const view_t *v, int hh, int hv, const vec3_t mins, const vec3_t maxs,
                    double *sx, double *sy);

#endif
