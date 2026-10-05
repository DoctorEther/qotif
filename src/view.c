/*
 * view.c - viewports (3D camera and 2D top/front/side) and mouse handling
 *
 * Mouse conventions (Hammer-like, with TrenchBroom-style camera):
 *   2D: LMB uses the current tool, MMB/RMB drag pans, RMB click opens the
 *       context menu, wheel zooms at the cursor.
 *   3D: LMB uses the current tool, RMB drag mouse-looks (WASD/QE fly while
 *       held), MMB drag strafes, wheel moves forward/back.
 */
#include "view.h"
#include "game.h"
#include "ui.h"

#define DRAG_THRESHOLD 3
#define HANDLE_PICK 6
#define HANDLE_OFFSET 6

view_t views[MAX_VIEWS];
view_t *active_view;
int fly_keys;
view_t *captured_view;

static pick_t down_pick;
static int down_hit;

static const char *type_names[VIEW_TYPE_COUNT] = {
    "camera", "top (x/y)", "front (y/z)", "side (x/z)"
};

void view_axes(viewtype_t type, int *h, int *v, int *d)
{
    switch (type) {
    case VIEW_FRONT:
        *h = 1; *v = 2; *d = 0;
        break;
    case VIEW_SIDE:
        *h = 0; *v = 2; *d = 1;
        break;
    default:
        *h = 0; *v = 1; *d = 2;
        break;
    }
}

const char *view_type_name(viewtype_t type)
{
    return (type >= 0 && type < VIEW_TYPE_COUNT) ? type_names[type] : "?";
}

const char *view_label(const view_t *v)
{
    static char buf[64];
    static const char *modes[] = { "textured", "flat", "wireframe" };
    if (v->type != VIEW_3D)
        return type_names[v->type];
    snprintf(buf, sizeof(buf), "camera - %s", modes[v->rmode]);
    return buf;
}

int view_label_hit(const view_t *v, int x, int y)
{
    return y >= 0 && y < 18 && x >= 0 && x < 12 + (int)strlen(view_label(v)) * 7;
}

void view_init(view_t *v, int index, viewtype_t type)
{
    void *ui = v->ui;
    memset(v, 0, sizeof(*v));
    v->ui = ui;
    v->index = index;
    v->zoom = 0.5;
    v->rmode = RENDER_TEXTURED;
    view_set_type(v, type);
}

void view_set_type(view_t *v, viewtype_t type)
{
    v->type = type;
    view_axes(type, &v->hax, &v->vax, &v->dax);
}

void view_to_world(const view_t *v, int x, int y, double *h, double *vv)
{
    *h = v->cx + (x - v->width * 0.5) / v->zoom;
    *vv = v->cy - (y - v->height * 0.5) / v->zoom;
}

void view_to_screen(const view_t *v, double h, double vv, double *x, double *y)
{
    *x = (h - v->cx) * v->zoom + v->width * 0.5;
    *y = v->height * 0.5 - (vv - v->cy) * v->zoom;
}

static double snapv(double x)
{
    return ed.snap ? snap_value(x, ed.grid) : floor(x + 0.5);
}

void camera_vectors(const camera_t *c, vec3_t fwd, vec3_t right, vec3_t up)
{
    double cy = cos(DEG2RAD(c->yaw)), sy = sin(DEG2RAD(c->yaw));
    double cp = cos(DEG2RAD(c->pitch)), sp = sin(DEG2RAD(c->pitch));
    v3_set(fwd, cp * cy, cp * sy, sp);
    v3_set(right, sy, -cy, 0);
    v3_cross(right, fwd, up);
}

void camera_look_at(const vec3_t target, double distance)
{
    vec3_t fwd, right, up;
    camera_vectors(&ed.cam, fwd, right, up);
    v3_ma(target, -distance, fwd, ed.cam.pos);
}

void view_ray(const view_t *v, int x, int y, vec3_t org, vec3_t dir)
{
    vec3_t fwd, right, up;
    double aspect = v->height > 0 ? (double)v->width / v->height : 1.0;
    double t = tan(DEG2RAD(prefs.fov) * 0.5);
    double nx = 2.0 * x / (v->width > 0 ? v->width : 1) - 1.0;
    double ny = 1.0 - 2.0 * y / (v->height > 0 ? v->height : 1);

    camera_vectors(&ed.cam, fwd, right, up);
    v3_copy(ed.cam.pos, org);
    v3_ma(fwd, nx * t * aspect, right, dir);
    v3_ma(dir, ny * t, up, dir);
    v3_normalize(dir);
}

int view_project(const view_t *v, const vec3_t p, double *sx, double *sy)
{
    vec3_t fwd, right, up, d;
    double aspect, t, z;

    if (v->type != VIEW_3D) {
        view_to_screen(v, p[v->hax], p[v->vax], sx, sy);
        return 1;
    }
    camera_vectors(&ed.cam, fwd, right, up);
    v3_sub(p, ed.cam.pos, d);
    z = v3_dot(d, fwd);
    if (z < 1.0)
        return 0;
    aspect = v->height > 0 ? (double)v->width / v->height : 1.0;
    t = tan(DEG2RAD(prefs.fov) * 0.5);
    *sx = (v3_dot(d, right) / (z * t * aspect) + 1.0) * 0.5 * v->width;
    *sy = (1.0 - v3_dot(d, up) / (z * t)) * 0.5 * v->height;
    return 1;
}

void view_center_on(view_t *v, const vec3_t p)
{
    if (v->type == VIEW_3D) {
        camera_look_at(p, 256);
    } else {
        v->cx = p[v->hax];
        v->cy = p[v->vax];
    }
}

void views_center_2d(const vec3_t p)
{
    int i;
    for (i = 0; i < MAX_VIEWS; i++)
        if (views[i].type != VIEW_3D)
            view_center_on(&views[i], p);
    ui_redraw_all();
}

int view_handle_pos(const view_t *v, int hh, int hv, const vec3_t mins, const vec3_t maxs,
                    double *sx, double *sy)
{
    double h, vv;

    if (hh == 0 && hv == 0)
        return 0;
    if (ed.handles == HANDLE_ROTATE && (hh == 0 || hv == 0))
        return 0;
    h = hh < 0 ? mins[v->hax] : hh > 0 ? maxs[v->hax] : (mins[v->hax] + maxs[v->hax]) * 0.5;
    vv = hv < 0 ? mins[v->vax] : hv > 0 ? maxs[v->vax] : (mins[v->vax] + maxs[v->vax]) * 0.5;
    view_to_screen(v, h, vv, sx, sy);
    *sx += hh * HANDLE_OFFSET;
    *sy -= hv * HANDLE_OFFSET;
    return 1;
}

static int hit_handle(const view_t *v, int x, int y, int *hh, int *hv)
{
    vec3_t mins, maxs;
    int i, j;

    if (!sel_bounds(mins, maxs))
        return 0;
    for (i = -1; i <= 1; i++)
        for (j = -1; j <= 1; j++) {
            double sx, sy;
            if (!view_handle_pos(v, i, j, mins, maxs, &sx, &sy))
                continue;
            if (fabs(x - sx) <= HANDLE_PICK && fabs(y - sy) <= HANDLE_PICK) {
                *hh = i;
                *hv = j;
                return 1;
            }
        }
    return 0;
}

/* ------------------------------------------------------------------ */
/* entity placement                                                    */

static eclass_t *current_point_class(void)
{
    eclass_t *ec = eclass_find(ed.entclass);
    if (!ec || ec->kind != EC_POINT) {
        ui_status("Choose a point entity class in the object bar first");
        return NULL;
    }
    return ec;
}

static void place_entity_on_surface(const pick_t *pk)
{
    eclass_t *ec = current_point_class();
    vec3_t n, origin, mins, maxs;
    double offset = -1e30;
    int i;

    if (!ec)
        return;
    if (pk->brush)
        v3_copy(pk->brush->faces[pk->face].plane.normal, n);
    else
        v3_set(n, 0, 0, 1);
    v3_copy(ec->mins, mins);
    v3_copy(ec->maxs, maxs);
    for (i = 0; i < 8; i++) {
        vec3_t c;
        double d;
        v3_set(c, (i & 1) ? maxs[0] : mins[0], (i & 2) ? maxs[1] : mins[1], (i & 4) ? maxs[2] : mins[2]);
        d = -v3_dot(n, c);
        if (d > offset)
            offset = d;
    }
    v3_ma(pk->point, offset, n, origin);
    for (i = 0; i < 3; i++)
        origin[i] = fabs(n[i]) > 0.999 ? floor(origin[i] + 0.5) : snapv(origin[i]);
    cmd_create_entity(ec->name, origin);
}

static void place_entity_2d(view_t *v, double h, double vv)
{
    eclass_t *ec = current_point_class();
    vec3_t origin, mins, maxs;

    if (!ec)
        return;
    v3_clear(origin);
    if (sel_bounds(mins, maxs))
        origin[v->dax] = snapv((mins[v->dax] + maxs[v->dax]) * 0.5);
    else if (v->dax == 2)
        origin[2] = -ec->mins[2];
    origin[v->hax] = snapv(h);
    origin[v->vax] = snapv(vv);
    cmd_create_entity(ec->name, origin);
}

/* ------------------------------------------------------------------ */
/* 2D                                                                  */

static void camera_2d(view_t *v, int x, int y, int mods)
{
    double h, vv;

    view_to_world(v, x, y, &h, &vv);
    if (mods & (MOD_SHIFT | MOD_CTRL)) {
        double dx = h - ed.cam.pos[v->hax], dy = vv - ed.cam.pos[v->vax];
        if (fabs(dx) < 1e-6 && fabs(dy) < 1e-6)
            return;
        if (v->type == VIEW_TOP) {
            ed.cam.yaw = RAD2DEG(atan2(dy, dx));
        } else {
            ed.cam.pitch = RAD2DEG(atan2(dy, fabs(dx)));
            if (v->hax == 0)
                ed.cam.yaw = dx >= 0 ? 0.0 : 180.0;
            else
                ed.cam.yaw = dx >= 0 ? 90.0 : 270.0;
        }
    } else {
        ed.cam.pos[v->hax] = h;
        ed.cam.pos[v->vax] = vv;
    }
    ui_redraw_all();
}

static void block_depth(const view_t *v, double *dmin, double *dmax)
{
    vec3_t mins, maxs;
    int d = v->dax;

    if (sel_bounds(mins, maxs) && maxs[d] - mins[d] >= 1.0) {
        *dmin = mins[d];
        *dmax = maxs[d];
    } else if (ed.have_last_block) {
        *dmin = ed.last_block_mins[d];
        *dmax = ed.last_block_maxs[d];
    } else {
        *dmin = 0;
        *dmax = 64;
    }
}

static void clip_down(view_t *v, int x, int y, double h, double vv)
{
    int i;

    if (ed.clip_npts && ed.clip_axis == v->dax) {
        for (i = 0; i < ed.clip_npts; i++) {
            double sx, sy;
            view_to_screen(v, ed.clip_pts[i][v->hax], ed.clip_pts[i][v->vax], &sx, &sy);
            if (fabs(sx - x) <= HANDLE_PICK && fabs(sy - y) <= HANDLE_PICK) {
                v->clip_drag = i;
                v->mode = MODE_CLIP;
                return;
            }
        }
    }
    ed.clip_axis = v->dax;
    ed.clip_npts = 1;
    v3_clear(ed.clip_pts[0]);
    ed.clip_pts[0][v->hax] = snapv(h);
    ed.clip_pts[0][v->vax] = snapv(vv);
    v3_copy(ed.clip_pts[0], ed.clip_pts[1]);
    v->clip_drag = 1;
    v->mode = MODE_CLIP;
    ui_redraw_all();
}

/* ------------------------------------------------------------------ */
/* face selection and vertex tool                                      */

/* selects the picked face (Ctrl toggles); whole_brush selects all of the
 * brush's faces; clicking empty space clears the face selection */
static void select_face(const pick_t *pk, int mods, int whole_brush)
{
    int i;

    if (!pk || !pk->brush) {
        if (!(mods & MOD_CTRL)) {
            sel_faces_clear();
            sel_changed();
        }
        return;
    }
    if (!(mods & MOD_CTRL))
        sel_faces_clear();
    if (whole_brush) {
        for (i = 0; i < pk->brush->nfaces; i++)
            pk->brush->faces[i].selected = 1;
    } else {
        face_t *f = &pk->brush->faces[pk->face];
        f->selected = (mods & MOD_CTRL) ? !f->selected : 1;
    }
    ui_status("%d face(s) selected", sel_face_count());
    sel_changed();
}

#define VTX_PICK 6

/* Picks vertex tool handles under the cursor.  In 2D every handle whose
 * projection is close is taken, so vertices hidden behind each other move
 * together (as in Hammer); in 3D only the nearest one.  Vertices win over
 * edges and edges over faces.  Returns 0 when nothing was hit. */
static int vertex_down(view_t *v, int x, int y, int mods)
{
    vhandle_t *hs;
    vec3_t *vs;
    int n = vtx_collect(&hs, &vs), i, kind, nw = 0, all = 1;
    int *which;
    double bestz = 1e30;

    if (!n) {
        free(hs);
        free(vs);
        return 0;
    }
    which = xmalloc(sizeof(int) * (size_t)n);
    for (kind = VH_VERTEX; kind <= VH_FACE && !nw; kind++)
        for (i = 0; i < n; i++) {
            double sx, sy;
            if (hs[i].kind != kind || !view_project(v, hs[i].pos, &sx, &sy))
                continue;
            if (fabs(sx - x) > VTX_PICK || fabs(sy - y) > VTX_PICK)
                continue;
            if (v->type == VIEW_3D) {
                vec3_t d;
                double z;
                v3_sub(hs[i].pos, ed.cam.pos, d);
                z = v3_len(d);
                if (z < bestz) {
                    bestz = z;
                    which[0] = i;
                    nw = 1;
                }
            } else {
                which[nw++] = i;
            }
        }
    if (nw) {
        if (mods & MOD_CTRL) {
            vtx_select(hs, which, nw, vs, SEL_TOGGLE);
        } else {
            for (i = 0; i < nw && all; i++)
                all = vtx_handle_selected(&hs[which[i]], vs);
            if (!all)
                vtx_select(hs, which, nw, vs, SEL_SET);
            v->mode = MODE_VERTEX;
            v3_copy(hs[which[0]].pos, v->vref);
            v3_clear(v->vdelta);
        }
        ui_status("%d vertex(es) selected", ed.nvsel);
        ui_redraw_all();
    }
    free(which);
    free(hs);
    free(vs);
    return nw > 0;
}

static void vertex_move(view_t *v, int x, int y, int mods)
{
    vec3_t d;
    int failed, i;

    if (!v->moved)
        return;
    v3_clear(d);
    if (v->type != VIEW_3D) {
        double h, vv;
        view_to_world(v, x, y, &h, &vv);
        d[v->hax] = snapv(v->vref[v->hax] + h - v->start_world[v->hax]) - v->vref[v->hax];
        d[v->vax] = snapv(v->vref[v->vax] + vv - v->start_world[v->vax]) - v->vref[v->vax];
    } else {
        /* horizontal plane through the handle; Alt moves vertically */
        vec3_t org, dir, n, p, fwd, right, up;
        double denom, t;

        view_ray(v, x, y, org, dir);
        if (mods & MOD_ALT) {
            camera_vectors(&ed.cam, fwd, right, up);
            v3_set(n, fwd[0], fwd[1], 0);
            if (v3_normalize(n) < 1e-6)
                return;
        } else {
            v3_set(n, 0, 0, 1);
        }
        denom = v3_dot(n, dir);
        if (fabs(denom) < 1e-6)
            return;
        t = (v3_dot(n, v->vref) - v3_dot(n, org)) / denom;
        if (t <= 0)
            return;
        v3_ma(org, t, dir, p);
        for (i = 0; i < 3; i++)
            d[i] = snapv(p[i]) - v->vref[i];
        if (mods & MOD_ALT)
            d[0] = d[1] = 0;
        else
            d[2] = 0;
    }
    if (!tr_active())
        vtx_drag_begin();
    v3_copy(d, v->vdelta);
    failed = vtx_drag_apply(d);
    if (failed)
        ui_status("Move vertices: %g %g %g (refused for %d brush(es): would not stay convex)",
                  d[0], d[1], d[2], failed);
    else
        ui_status("Move vertices: %g %g %g", d[0], d[1], d[2]);
    ui_redraw_all();
}

static void select_down(view_t *v, int x, int y, double h, double vv, int mods)
{
    int hh, hv;
    vec3_t mins, maxs;

    if (ed.tool != TOOL_VERTEX && hit_handle(v, x, y, &hh, &hv)) {
        v->mode = MODE_HANDLE;
        v->handle_h = hh;
        v->handle_v = hv;
        sel_bounds(v->orig_mins, v->orig_maxs);
        return;
    }
    down_hit = pick_2d(v->hax, v->vax, v->dax, h, vv, &down_pick);
    if (!(mods & MOD_CTRL) && sel_bounds(mins, maxs)
        && h >= mins[v->hax] && h <= maxs[v->hax] && vv >= mins[v->vax] && vv <= maxs[v->vax]) {
        v->mode = MODE_MOVE;
        v->click_on_selected = 1;
        return;
    }
    if (down_hit) {
        sel_pick(&down_pick, (mods & MOD_CTRL) ? SEL_TOGGLE : SEL_SET);
        sel_changed();
        if (!(mods & MOD_CTRL)) {
            v->mode = MODE_MOVE;
            v->click_on_selected = 0;
        }
        return;
    }
    v->mode = MODE_RUBBER;
}

static void down_2d(view_t *v, int button, int x, int y, int mods)
{
    double h, vv;

    view_to_world(v, x, y, &h, &vv);
    v3_clear(v->start_world);
    v->start_world[v->hax] = h;
    v->start_world[v->vax] = vv;

    if (button == BTN_MIDDLE || button == BTN_RIGHT) {
        v->mode = MODE_PAN;
        return;
    }
    if (button != BTN_LEFT)
        return;
    switch (ed.tool) {
    case TOOL_CAMERA:
        v->mode = MODE_CAMERA;
        camera_2d(v, x, y, mods);
        break;
    case TOOL_BLOCK:
        v->mode = MODE_BLOCK;
        v->start_world[v->hax] = snapv(h);
        v->start_world[v->vax] = snapv(vv);
        ed.preview = 0;
        break;
    case TOOL_ENTITY:
        place_entity_2d(v, h, vv);
        break;
    case TOOL_CLIP:
        clip_down(v, x, y, h, vv);
        break;
    case TOOL_VERTEX:
        if (!vertex_down(v, x, y, mods))
            select_down(v, x, y, h, vv, mods);
        break;
    default:
        select_down(v, x, y, h, vv, mods);
        break;
    }
}

static void move_2d(view_t *v, int x, int y, int mods)
{
    double h, vv;
    vec3_t t;
    mat3_t m;

    view_to_world(v, x, y, &h, &vv);
    switch (v->mode) {
    case MODE_PAN:
        v->cx -= (x - v->last_x) / v->zoom;
        v->cy += (y - v->last_y) / v->zoom;
        ui_redraw_view(v);
        break;

    case MODE_CAMERA:
        camera_2d(v, x, y, mods);
        break;

    case MODE_MOVE:
        if (!v->moved)
            return;
        if (!tr_active()) {
            if (mods & MOD_SHIFT) {
                undo_push("Clone");
                sel_duplicate_in_place();
            } else {
                undo_push("Move");
            }
            sel_bounds(v->orig_mins, v->orig_maxs);
            tr_begin();
        }
        v3_clear(t);
        t[v->hax] = snapv(v->orig_mins[v->hax] + h - v->start_world[v->hax]) - v->orig_mins[v->hax];
        t[v->vax] = snapv(v->orig_mins[v->vax] + vv - v->start_world[v->vax]) - v->orig_mins[v->vax];
        mat3_identity(m);
        tr_apply(m, t);
        ui_status("Move: %g %g %g", t[0], t[1], t[2]);
        ui_redraw_all();
        break;

    case MODE_HANDLE:
        if (!v->moved)
            return;
        if (!tr_active()) {
            undo_push(ed.handles == HANDLE_SCALE ? "Scale" : "Rotate");
            tr_begin();
        }
        if (ed.handles == HANDLE_SCALE) {
            vec3_t nmins, nmaxs;
            int axes[2], dirs[2], i;
            v3_copy(v->orig_mins, nmins);
            v3_copy(v->orig_maxs, nmaxs);
            axes[0] = v->hax; dirs[0] = v->handle_h;
            axes[1] = v->vax; dirs[1] = v->handle_v;
            for (i = 0; i < 2; i++) {
                int a = axes[i];
                double delta = (i == 0 ? h : vv) - v->start_world[a];
                if (dirs[i] < 0) {
                    nmins[a] = snapv(v->orig_mins[a] + delta);
                    if (nmins[a] > nmaxs[a] - 1)
                        nmins[a] = nmaxs[a] - 1;
                } else if (dirs[i] > 0) {
                    nmaxs[a] = snapv(v->orig_maxs[a] + delta);
                    if (nmaxs[a] < nmins[a] + 1)
                        nmaxs[a] = nmins[a] + 1;
                }
            }
            mat3_identity(m);
            v3_clear(t);
            for (i = 0; i < 3; i++) {
                double ext = v->orig_maxs[i] - v->orig_mins[i];
                double s = ext > 1e-6 ? (nmaxs[i] - nmins[i]) / ext : 1.0;
                m[i][i] = s;
                t[i] = nmins[i] - s * v->orig_mins[i];
            }
            tr_apply(m, t);
            ui_status("Size: %g x %g x %g", nmaxs[0] - nmins[0], nmaxs[1] - nmins[1], nmaxs[2] - nmins[2]);
        } else {
            double ch = (v->orig_mins[v->hax] + v->orig_maxs[v->hax]) * 0.5;
            double cv = (v->orig_mins[v->vax] + v->orig_maxs[v->vax]) * 0.5;
            double a0 = atan2(v->start_world[v->vax] - cv, v->start_world[v->hax] - ch);
            double a1 = atan2(vv - cv, h - ch);
            double deg = RAD2DEG(a1 - a0);
            vec3_t eh, ev, ecr, c, mc;
            double sign;
            int i;

            if (!(mods & MOD_SHIFT))
                deg = floor(deg / 15.0 + 0.5) * 15.0;
            v3_clear(eh); eh[v->hax] = 1;
            v3_clear(ev); ev[v->vax] = 1;
            v3_cross(eh, ev, ecr);
            sign = ecr[v->dax] >= 0 ? 1.0 : -1.0;
            mat3_rotation(m, v->dax, deg * sign);
            for (i = 0; i < 3; i++)
                c[i] = (v->orig_mins[i] + v->orig_maxs[i]) * 0.5;
            mat3_mul_vec(m, c, mc);
            v3_sub(c, mc, t);
            tr_apply(m, t);
            ui_status("Rotate: %g degrees", deg);
        }
        ui_redraw_all();
        break;

    case MODE_RUBBER:
        ui_redraw_view(v);
        break;

    case MODE_BLOCK:
        if (!v->moved)
            return;
        {
            double h0 = v->start_world[v->hax], v0 = v->start_world[v->vax];
            double h1 = snapv(h), v1 = snapv(vv), dmin, dmax;
            block_depth(v, &dmin, &dmax);
            ed.preview_mins[v->hax] = h0 < h1 ? h0 : h1;
            ed.preview_maxs[v->hax] = h0 < h1 ? h1 : h0;
            ed.preview_mins[v->vax] = v0 < v1 ? v0 : v1;
            ed.preview_maxs[v->vax] = v0 < v1 ? v1 : v0;
            ed.preview_mins[v->dax] = dmin;
            ed.preview_maxs[v->dax] = dmax;
            ed.preview = 1;
            ui_status("Block: %g x %g x %g",
                      ed.preview_maxs[0] - ed.preview_mins[0],
                      ed.preview_maxs[1] - ed.preview_mins[1],
                      ed.preview_maxs[2] - ed.preview_mins[2]);
        }
        ui_redraw_all();
        break;

    case MODE_CLIP:
        ed.clip_pts[v->clip_drag][v->hax] = snapv(h);
        ed.clip_pts[v->clip_drag][v->vax] = snapv(vv);
        if (v->clip_drag == 1 && ed.clip_npts == 1 && v->moved)
            ed.clip_npts = 2;
        ui_redraw_all();
        break;

    default:
        break;
    }
}

static void up_2d(view_t *v, int mode, int x, int y, int mods)
{
    double h, vv;

    view_to_world(v, x, y, &h, &vv);
    switch (mode) {
    case MODE_PAN:
        if (!v->moved && v->button == BTN_RIGHT) {
            v3_clear(v->ctx_point);
            v->ctx_point[v->hax] = snapv(h);
            v->ctx_point[v->vax] = snapv(vv);
            ui_popup_context(v, x, y);
        }
        break;

    case MODE_MOVE:
        if (tr_active()) {
            tr_end(1);
            sel_changed();
        } else if (v->click_on_selected) {
            if (down_hit && !pick_is_selected(&down_pick)) {
                sel_pick(&down_pick, SEL_SET);
            } else {
                ed.handles = ed.handles == HANDLE_SCALE ? HANDLE_ROTATE : HANDLE_SCALE;
                ui_status(ed.handles == HANDLE_SCALE ? "Scale handles" : "Rotate handles");
            }
            sel_changed();
        }
        break;

    case MODE_HANDLE:
        if (tr_active()) {
            tr_end(1);
            sel_changed();
        }
        break;

    case MODE_RUBBER:
        if (!(mods & MOD_CTRL))
            sel_clear();
        if (v->moved) {
            double h0, v0;
            view_to_world(v, v->press_x, v->press_y, &h0, &v0);
            sel_box(v->hax, v->vax, h0, v0, h, vv);
        }
        sel_changed();
        break;

    case MODE_BLOCK:
        if (ed.preview) {
            vec3_t mins, maxs;
            v3_copy(ed.preview_mins, mins);
            v3_copy(ed.preview_maxs, maxs);
            ed.preview = 0;
            if (maxs[v->hax] - mins[v->hax] >= 1 && maxs[v->vax] - mins[v->vax] >= 1
                && maxs[v->dax] - mins[v->dax] >= 1)
                cmd_create_brush(mins, maxs, v->hax, v->vax, v->dax);
        }
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 3D                                                                  */

static void down_3d(view_t *v, int button, int x, int y, int mods)
{
    vec3_t org, dir;
    pick_t pk;
    int hit;

    if (button == BTN_RIGHT || (button == BTN_LEFT && ed.tool == TOOL_CAMERA)) {
        v->mode = MODE_LOOK;
        ui_set_cursor(v, CURSOR_HIDDEN);
        v->last_x = v->width / 2;
        v->last_y = v->height / 2;
        ui_warp_pointer(v, v->last_x, v->last_y);
        return;
    }
    if (button == BTN_MIDDLE) {
        v->mode = MODE_STRAFE;
        return;
    }
    if (button != BTN_LEFT)
        return;

    if (ed.tool == TOOL_VERTEX && vertex_down(v, x, y, mods))
        return;
    view_ray(v, x, y, org, dir);
    hit = pick_ray(org, dir, &pk);
    switch (ed.tool) {
    case TOOL_TEXTURE:
        if (hit && pk.brush && (mods & MOD_ALT)) {
            const face_t *f = &pk.brush->faces[pk.face];
            str_copy(ed.texture, f->tex, sizeof(ed.texture));
            ui_textures_changed();
            ui_status("Texture: %s", f->tex);
            return;
        }
        /* Shift selects every face of the brush */
        select_face(hit ? &pk : NULL, mods, (mods & MOD_SHIFT) != 0);
        break;

    case TOOL_ENTITY:
        if (hit) {
            place_entity_on_surface(&pk);
        } else {
            pick_t fake;
            vec3_t fwd, right, up;
            memset(&fake, 0, sizeof(fake));
            camera_vectors(&ed.cam, fwd, right, up);
            v3_ma(ed.cam.pos, 128, dir, fake.point);
            place_entity_on_surface(&fake);
        }
        break;

    default:
        /* Shift+click picks single faces with any tool (TrenchBroom) */
        if (mods & MOD_SHIFT) {
            select_face(hit ? &pk : NULL, mods, 0);
            break;
        }
        if (hit)
            sel_pick(&pk, (mods & MOD_CTRL) ? SEL_TOGGLE : SEL_SET);
        else if (!(mods & MOD_CTRL))
            sel_clear();
        sel_changed();
        break;
    }
}

/* turns the camera by the pointer offset and re-centers the pointer; the
 * motion event the warp itself generates has no offset and is ignored */
static void look_step(view_t *v, int x, int y)
{
    int dx = x - v->last_x, dy = y - v->last_y;

    if (!dx && !dy)
        return;
    v->accum += abs(dx) + abs(dy);
    ed.cam.yaw -= dx * prefs.sensitivity;
    ed.cam.pitch -= dy * prefs.sensitivity * (prefs.invert_mouse ? -1.0 : 1.0);
    if (ed.cam.pitch > 89.0)
        ed.cam.pitch = 89.0;
    if (ed.cam.pitch < -89.0)
        ed.cam.pitch = -89.0;
    ed.cam.yaw = fmod(ed.cam.yaw, 360.0);
    v->last_x = v->width / 2;
    v->last_y = v->height / 2;
    ui_warp_pointer(v, v->last_x, v->last_y);
    ui_redraw_all();
}

void view_set_capture(view_t *v, int on)
{
    if (!on) {
        view_t *c = captured_view;
        if (!c)
            return;
        captured_view = NULL;
        ui_grab_pointer(c, 0);
        ui_set_cursor(c, CURSOR_NORMAL);
        fly_keys = 0;
        ui_status("Mouse released");
        return;
    }
    if (!v || v->type != VIEW_3D)
        return;
    if (captured_view)
        view_set_capture(NULL, 0);
    active_view = v;
    v->last_x = v->width / 2;
    v->last_y = v->height / 2;
    if (!ui_grab_pointer(v, 1)) {
        ui_status("Could not capture the mouse");
        return;
    }
    captured_view = v;
    ui_warp_pointer(v, v->last_x, v->last_y);
    ui_status("Mouse captured: move to look, WASD/QE to fly, Z or Escape to release");
    ui_redraw_all();
}

static void move_3d(view_t *v, int x, int y, int mods)
{
    int dx = x - v->last_x, dy = y - v->last_y;
    vec3_t fwd, right, up;

    switch (v->mode) {
    case MODE_LOOK:
        look_step(v, x, y);
        return;

    case MODE_STRAFE:
        camera_vectors(&ed.cam, fwd, right, up);
        v3_ma(ed.cam.pos, dx * ((mods & MOD_SHIFT) ? 4.0 : 1.0), right, ed.cam.pos);
        v3_ma(ed.cam.pos, -dy * ((mods & MOD_SHIFT) ? 4.0 : 1.0), up, ed.cam.pos);
        ui_redraw_all();
        break;

    default:
        break;
    }
}

static void up_3d(view_t *v, int mode, int button)
{
    if (mode != MODE_LOOK)
        return;
    if (v != captured_view) {
        ui_warp_pointer(v, v->press_x, v->press_y);
        ui_set_cursor(v, CURSOR_NORMAL);
    }
    if (button == BTN_RIGHT && v->accum < 4 && ed.tool == TOOL_TEXTURE) {
        /* right click applies the current texture to the face (Hammer) */
        vec3_t org, dir;
        pick_t pk;
        view_ray(v, v->press_x, v->press_y, org, dir);
        if (pick_ray(org, dir, &pk) && pk.brush) {
            undo_push("Apply Texture");
            str_copy(pk.brush->faces[pk.face].tex, ed_default_texture(), sizeof(pk.brush->faces[pk.face].tex));
            sel_changed();
        }
    }
}

/* ------------------------------------------------------------------ */
/* entry points                                                        */

void view_mouse_down(view_t *v, int button, int x, int y, int mods)
{
    active_view = v;
    if (v->mode != MODE_NONE)
        return;
    v->button = button;
    v->mods = mods;
    v->press_x = v->last_x = v->mouse_x = x;
    v->press_y = v->last_y = v->mouse_y = y;
    v->moved = 0;
    v->accum = 0;
    if (button == BTN_LEFT && view_label_hit(v, x, y)) {
        ui_popup_view_menu(v, x, y);
        return;
    }
    if (v->type == VIEW_3D)
        down_3d(v, button, x, y, mods);
    else
        down_2d(v, button, x, y, mods);
}

void view_mouse_move(view_t *v, int x, int y, int mods)
{
    v->mouse_x = x;
    v->mouse_y = y;
    if (v == captured_view && v->mode == MODE_NONE) {
        look_step(v, x, y);
        return;
    }
    if (v->mode == MODE_NONE) {
        if (v->type != VIEW_3D) {
            static const char axis[] = "xyz";
            double h, vv;
            view_to_world(v, x, y, &h, &vv);
            ui_status("%c: %g  %c: %g", axis[v->hax], snapv(h), axis[v->vax], snapv(vv));
        }
        return;
    }
    if (!v->moved && (abs(x - v->press_x) > DRAG_THRESHOLD || abs(y - v->press_y) > DRAG_THRESHOLD))
        v->moved = 1;
    if (v->mode == MODE_VERTEX)
        vertex_move(v, x, y, mods);
    else if (v->type == VIEW_3D)
        move_3d(v, x, y, mods);
    else
        move_2d(v, x, y, mods);
    if (v->mode != MODE_LOOK) {
        v->last_x = x;
        v->last_y = y;
    }
}

void view_mouse_up(view_t *v, int button, int x, int y, int mods)
{
    int mode;

    if (v->mode == MODE_NONE || button != v->button)
        return;
    mode = v->mode;
    v->mode = MODE_NONE;
    if (mode == MODE_VERTEX) {
        if (tr_active()) {
            vtx_drag_end(1, v->vdelta);
            sel_changed();
        }
    } else if (v->type == VIEW_3D)
        up_3d(v, mode, button);
    else
        up_2d(v, mode, x, y, mods);
    ui_redraw_all();
}

void view_wheel(view_t *v, int dir, int x, int y, int mods)
{
    active_view = v;
    if (v->type == VIEW_3D) {
        vec3_t fwd, right, up;
        camera_vectors(&ed.cam, fwd, right, up);
        v3_ma(ed.cam.pos, dir * ((mods & MOD_SHIFT) ? 256.0 : 64.0), fwd, ed.cam.pos);
    } else {
        double h, vv;
        view_to_world(v, x, y, &h, &vv);
        v->zoom *= dir > 0 ? 1.25 : 0.8;
        if (v->zoom < 0.01)
            v->zoom = 0.01;
        if (v->zoom > 64.0)
            v->zoom = 64.0;
        v->cx = h - (x - v->width * 0.5) / v->zoom;
        v->cy = vv + (y - v->height * 0.5) / v->zoom;
    }
    ui_redraw_all();
}

void view_mouse_leave(view_t *v)
{
    (void)v;
}

int view_cancel(void)
{
    int i, any = 0;

    if (captured_view) {
        view_set_capture(NULL, 0);
        any = 1;
    }
    for (i = 0; i < MAX_VIEWS; i++) {
        if (views[i].mode != MODE_NONE) {
            if (views[i].mode == MODE_LOOK) {
                ui_set_cursor(&views[i], CURSOR_NORMAL);
            }
            views[i].mode = MODE_NONE;
            any = 1;
        }
    }
    if (tr_active()) {
        tr_end(0);
        any = 1;
    }
    if (ed.preview) {
        ed.preview = 0;
        any = 1;
    }
    if (ed.clip_npts) {
        ed.clip_npts = 0;
        any = 1;
    }
    if (any)
        ui_redraw_all();
    return any;
}

void fly_step(double dt, int fast)
{
    vec3_t fwd, right, up, move;
    double speed = prefs.flyspeed * dt * (fast ? 3.0 : 1.0);

    if (!fly_keys)
        return;
    camera_vectors(&ed.cam, fwd, right, up);
    v3_clear(move);
    if (fly_keys & FLY_FORWARD)
        v3_add(move, fwd, move);
    if (fly_keys & FLY_BACK)
        v3_sub(move, fwd, move);
    if (fly_keys & FLY_RIGHT)
        v3_add(move, right, move);
    if (fly_keys & FLY_LEFT)
        v3_sub(move, right, move);
    if (fly_keys & FLY_UP)
        move[2] += 1.0;
    if (fly_keys & FLY_DOWN)
        move[2] -= 1.0;
    if (v3_normalize(move) > 0) {
        v3_ma(ed.cam.pos, speed, move, ed.cam.pos);
        ui_redraw_all();
    }
}
