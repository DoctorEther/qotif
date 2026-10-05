/*
 * editor.c - editor state, selection and editing commands
 */
#include "editor.h"
#include "game.h"
#include "textures.h"
#include "ui.h"
#include "vfs.h"
#include "view.h"

#include <ctype.h>

editor_t ed;

/* ------------------------------------------------------------------ */
/* state                                                               */

void ed_init(void)
{
    memset(&ed, 0, sizeof(ed));
    ed.grid = prefs.grid;
    ed.snap = prefs.snap;
    ed.show_grid = prefs.show_grid;
    ed.texlock = prefs.texlock;
    ed.prim = PRIM_BLOCK;
    ed.prim_sides = 8;
    ed.handles = HANDLE_SCALE;
    ed.clip_mode = CLIP_BOTH;
    str_copy(ed.entclass, "info_player_start", sizeof(ed.entclass));
    v3_set(ed.cam.pos, -320, -320, 224);
    ed.cam.yaw = 45.0;
    ed.cam.pitch = -25.0;
    ed.map = map_create();
}

void ed_mark_dirty(void)
{
    if (!ed.map->dirty) {
        ed.map->dirty = 1;
        ui_map_changed();
    }
}

void ed_set_map(map_t *m)
{
    if (tr_active())
        tr_end(0);
    map_free(ed.map);
    ed.map = m;
    ed.clip_npts = 0;
    vtx_clear();
    ed.preview = 0;
    eclass_bind_map(m);
    undo_clear();
}

static void place_camera_at_start(void)
{
    entity_t *e;
    for (e = ed.map->entities; e; e = e->next) {
        if (str_ieq(ent_classname(e), "info_player_start")) {
            const char *a = ent_get(e, "angle");
            v3_copy(e->origin, ed.cam.pos);
            ed.cam.pos[2] += 22;
            ed.cam.yaw = a ? atof(a) : 0.0;
            ed.cam.pitch = 0.0;
            views_center_2d(e->origin);
            return;
        }
    }
}

void ed_new_map(void)
{
    game_t *g = game_current();
    map_t *m = map_create();

    if (g && g->valve220) {
        m->valve220 = 1;
        ent_set(m->world, "mapversion", "220");
    }
    if (g && g->wads[0])
        ent_set(m->world, "wad", g->wads);
    ed_set_map(m);
    ed_load_resources();
    m->dirty = 0;
    ui_map_changed();
    sel_changed();
}

int ed_load_map(const char *path)
{
    char err[512];
    map_t *m = map_load(path, err, sizeof(err));

    if (!m) {
        char msg[700];
        snprintf(msg, sizeof(msg), "Cannot load %s:\n%s", path, err);
        log_error("%s", msg);
        ui_message(msg);
        return 0;
    }
    ed_set_map(m);
    ed_load_resources();
    place_camera_at_start();
    m->dirty = 0;
    log_info("loaded %s: %d entities, %d brushes%s", path, map_count_entities(m),
             map_count_brushes(m), m->valve220 ? " (Valve 220)" : "");
    ui_map_changed();
    sel_changed();
    return 1;
}

int ed_save_map(const char *path)
{
    if (ed.map->valve220)
        ent_set(ed.map->world, "mapversion", "220");
    if (!map_save(ed.map, path)) {
        char msg[PATH_LEN + 64];
        snprintf(msg, sizeof(msg), "Cannot save %s", path);
        ui_message(msg);
        return 0;
    }
    str_copy(ed.map->path, path, sizeof(ed.map->path));
    ed.map->dirty = 0;
    log_info("saved %s", path);
    ui_map_changed();
    return 1;
}

static int resolve_wad(const char *name, char *out, size_t size)
{
    game_t *g = game_current();
    const char *gamepath = g ? prefs_game_get(g->name, "path", "") : "";
    const char *mod = g ? prefs_game_get(g->name, "mod", "") : "";
    char clean[PATH_LEN], mapdir[PATH_LEN], tmp[PATH_LEN];
    const char *base;
    const char *dirs[6];
    int i, n = 0;
    char *p;

    str_copy(clean, name, sizeof(clean));
    for (p = clean; *p; p++)
        if (*p == '\\')
            *p = '/';
    if (path_is_absolute(clean) && file_exists(clean)) {
        str_copy(out, clean, size);
        return 1;
    }
    base = path_basename(clean);
    mapdir[0] = 0;
    if (ed.map->path[0])
        path_dirname(mapdir, sizeof(mapdir), ed.map->path);

    if (*gamepath) {
        static char d1[PATH_LEN], d2[PATH_LEN];
        dirs[n++] = gamepath;
        path_join(d1, sizeof(d1), gamepath, g->basedir);
        dirs[n++] = d1;
        if (*mod) {
            path_join(d2, sizeof(d2), gamepath, mod);
            dirs[n++] = d2;
        }
    }
    if (mapdir[0])
        dirs[n++] = mapdir;

    for (i = 0; i < n; i++) {
        const char *rel = clean[0] == '/' ? clean + 1 : clean;
        path_join(tmp, sizeof(tmp), dirs[i], rel);
        if (file_exists(tmp)) {
            str_copy(out, tmp, size);
            return 1;
        }
    }
    for (i = 0; i < n; i++) {
        char sub[PATH_LEN];
        path_join(tmp, sizeof(tmp), dirs[i], base);
        if (file_exists(tmp)) {
            str_copy(out, tmp, size);
            return 1;
        }
        path_join(sub, sizeof(sub), dirs[i], "gfx");
        path_join(tmp, sizeof(tmp), sub, base);
        if (file_exists(tmp)) {
            str_copy(out, tmp, size);
            return 1;
        }
    }
    if (find_data_file(clean, out, size))
        return 1;
    snprintf(tmp, sizeof(tmp), "wads/%s", base);
    return find_data_file(tmp, out, size);
}

void ed_load_resources(void)
{
    game_t *g = game_current();
    const char *gamepath, *mod, *def;
    char list[2048], path[PATH_LEN];
    char *items[64];
    int n, i;

    if (!g)
        return;
    str_copy(map_game_name, g->name, sizeof(map_game_name));
    gamepath = prefs_game_get(g->name, "path", "");
    mod = prefs_game_get(g->name, "mod", "");

    vfs_shutdown();
    if (*gamepath) {
        path_join(path, sizeof(path), gamepath, g->basedir);
        vfs_add_gamedir(path);
        if (*mod && strcmp(mod, g->basedir)) {
            path_join(path, sizeof(path), gamepath, mod);
            vfs_add_gamedir(path);
        }
    } else {
        log_warn("no game path set for %s (Edit > Preferences > Game)", g->name);
    }

    /* palette */
    tex_default_palette();
    if (g->palette[0]) {
        size_t len = 0;
        unsigned char *data = vfs_load(g->palette, &len);
        if (!data) {
            path_join(path, sizeof(path), g->cfgdir, g->palette);
            if (file_exists(path) || find_data_file(g->palette, path, sizeof(path)))
                data = (unsigned char *)file_read_all(path, &len);
        }
        if (data && len >= 768)
            tex_set_palette(data);
        else
            log_warn("palette '%s' not found; WAD2 textures will be shown in grayscale", g->palette);
        free(data);
    }

    /* textures: game defaults plus the worldspawn "wad" key */
    tex_clear();
    snprintf(list, sizeof(list), "%s;%s", g->wads, ent_get(ed.map->world, "wad") ? ent_get(ed.map->world, "wad") : "");
    n = str_split(list, ';', items, 64);
    for (i = 0; i < n; i++) {
        int j, dup = 0;
        for (j = 0; j < i; j++)
            if (!strcmp(items[i], items[j]))
                dup = 1;
        if (dup)
            continue;
        if (resolve_wad(items[i], path, sizeof(path)))
            tex_load_wad(path);
        else
            log_warn("wad '%s' not found", items[i]);
    }

    /* entity definitions: TrenchBroom's _tb_def key overrides the game file */
    eclass_clear();
    def = ent_get(ed.map->world, "_tb_def");
    path[0] = 0;
    if (def && str_iprefix(def, "external:")) {
        str_copy(path, def + 9, sizeof(path));
    } else if (def && str_iprefix(def, "builtin:")) {
        char rel[PATH_LEN + 8];
        snprintf(rel, sizeof(rel), "games/%s", def + 8);
        if (!find_data_file(rel, path, sizeof(path)))
            path_join(path, sizeof(path), g->cfgdir, def + 8);
    } else if (g->entities[0]) {
        path_join(path, sizeof(path), g->cfgdir, g->entities);
        if (!file_exists(path)) {
            char rel[PATH_LEN + 8];
            snprintf(rel, sizeof(rel), "games/%s", g->entities);
            find_data_file(rel, path, sizeof(path));
        }
    }
    if (path[0])
        eclass_load(path);
    else
        log_warn("no entity definitions configured for %s", g->name);
    eclass_bind_map(ed.map);

    if (!ed.texture[0] || !tex_find(ed.texture)) {
        if (g->defaulttex[0])
            str_copy(ed.texture, g->defaulttex, sizeof(ed.texture));
        else if (tex_count())
            str_copy(ed.texture, tex_get(0)->name, sizeof(ed.texture));
    }
    ui_textures_changed();
    ui_entities_changed();
    ui_redraw_all();
}

const char *ed_default_texture(void)
{
    game_t *g;
    if (ed.texture[0])
        return ed.texture;
    g = game_current();
    if (g && g->defaulttex[0])
        return g->defaulttex;
    if (tex_count())
        return tex_get(0)->name;
    return "__TB_empty";
}

void ed_set_tool(tool_t t)
{
    if (tr_active())
        tr_end(0);
    if (t != ed.tool)
        vtx_clear();
    if (t != TOOL_CLIP)
        ed.clip_npts = 0;
    ed.preview = 0;
    ed.tool = t;
    ui_tool_changed();
    if (t == TOOL_TEXTURE)
        ui_open_face_dialog();
    ui_redraw_all();
}

void ed_set_grid(int grid)
{
    if (grid < 1)
        grid = 1;
    if (grid > 512)
        grid = 512;
    ed.grid = grid;
    prefs.grid = grid;
    ui_status("Grid: %d", grid);
    ui_sync_toggles();
    ui_redraw_all();
}

int ed_load_pointfile(const char *path)
{
    size_t len;
    char *text = file_read_all(path, &len);
    char *line, *next;
    int n = 0, max = 0;
    vec3_t *pts = NULL;

    if (!text) {
        ui_message("Cannot read the point file.");
        return 0;
    }
    for (line = text; line && *line; line = next) {
        double x, y, z;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        if (sscanf(line, "%lf %lf %lf", &x, &y, &z) != 3)
            continue;
        if (n >= max) {
            max = max ? max * 2 : 256;
            pts = xrealloc(pts, sizeof(vec3_t) * (size_t)max);
        }
        v3_set(pts[n], x, y, z);
        n++;
    }
    free(text);
    free(ed.pointfile);
    ed.pointfile = pts;
    ed.npointfile = n;
    log_info("point file %s: %d points", path, n);
    if (n) {
        views_center_2d(pts[0]);
        v3_copy(pts[0], ed.cam.pos);
    }
    ui_redraw_all();
    return n > 0;
}

void ed_clear_pointfile(void)
{
    free(ed.pointfile);
    ed.pointfile = NULL;
    ed.npointfile = 0;
    ui_redraw_all();
}

/* ------------------------------------------------------------------ */
/* selection                                                           */

int sel_count(void)
{
    entity_t *e;
    brush_t *b;
    int n = 0;
    for (e = ed.map->entities; e; e = e->next) {
        if (ent_is_point(e) && e->selected)
            n++;
        for (b = e->brushes; b; b = b->next)
            if (b->selected)
                n++;
    }
    return n;
}

int sel_brush_count(void)
{
    entity_t *e;
    brush_t *b;
    int n = 0;
    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next)
            if (b->selected)
                n++;
    return n;
}

int sel_bounds(vec3_t mins, vec3_t maxs)
{
    entity_t *e;
    brush_t *b;
    int n = 0;

    bounds_clear(mins, maxs);
    for (e = ed.map->entities; e; e = e->next) {
        if (ent_is_point(e) && e->selected) {
            vec3_t a, c;
            ent_bounds(e, a, c);
            bounds_add(mins, maxs, a);
            bounds_add(mins, maxs, c);
            n++;
        }
        for (b = e->brushes; b; b = b->next)
            if (b->selected && b->valid) {
                bounds_add(mins, maxs, b->mins);
                bounds_add(mins, maxs, b->maxs);
                n++;
            }
    }
    return n > 0;
}

void sel_clear(void)
{
    entity_t *e;
    brush_t *b;
    int i;
    for (e = ed.map->entities; e; e = e->next) {
        e->selected = 0;
        for (b = e->brushes; b; b = b->next) {
            b->selected = 0;
            for (i = 0; i < b->nfaces; i++)
                b->faces[i].selected = 0;
        }
    }
}

void sel_brush(brush_t *b, int mode)
{
    entity_t *e = b->owner;
    int state;

    if (mode == SEL_SET)
        sel_clear();
    state = mode == SEL_TOGGLE ? !b->selected : 1;
    if (e && e != ed.map->world && !ed.ignore_groups) {
        brush_t *o;
        for (o = e->brushes; o; o = o->next)
            o->selected = state;
    } else {
        b->selected = state;
    }
}

void sel_entity(entity_t *e, int mode)
{
    if (mode == SEL_SET)
        sel_clear();
    e->selected = mode == SEL_TOGGLE ? !e->selected : 1;
}

void sel_pick(const pick_t *p, int mode)
{
    if (p->brush)
        sel_brush(p->brush, mode);
    else if (p->ent)
        sel_entity(p->ent, mode);
}

int pick_is_selected(const pick_t *p)
{
    if (p->brush)
        return p->brush->selected;
    return p->ent ? p->ent->selected : 0;
}

void sel_all(void)
{
    entity_t *e;
    brush_t *b;
    for (e = ed.map->entities; e; e = e->next) {
        if (ent_is_point(e))
            e->selected = 1;
        for (b = e->brushes; b; b = b->next)
            b->selected = 1;
    }
}

void sel_invert(void)
{
    entity_t *e;
    brush_t *b;
    for (e = ed.map->entities; e; e = e->next) {
        if (ent_is_point(e))
            e->selected = !e->selected;
        for (b = e->brushes; b; b = b->next)
            b->selected = !b->selected;
    }
}

static int box_inside(const vec3_t mins, const vec3_t maxs, int hax, int vax,
                      double h0, double v0, double h1, double v1)
{
    return mins[hax] >= h0 && maxs[hax] <= h1 && mins[vax] >= v0 && maxs[vax] <= v1;
}

void sel_box(int hax, int vax, double h0, double v0, double h1, double v1)
{
    entity_t *e;
    brush_t *b;
    double t;

    if (h0 > h1) { t = h0; h0 = h1; h1 = t; }
    if (v0 > v1) { t = v0; v0 = v1; v1 = t; }
    for (e = ed.map->entities; e; e = e->next) {
        if (ent_is_point(e)) {
            vec3_t mins, maxs;
            ent_bounds(e, mins, maxs);
            if (box_inside(mins, maxs, hax, vax, h0, v0, h1, v1))
                e->selected = 1;
            continue;
        }
        for (b = e->brushes; b; b = b->next)
            if (b->valid && box_inside(b->mins, b->maxs, hax, vax, h0, v0, h1, v1))
                sel_brush(b, SEL_ADD);
    }
}

int sel_entities(entity_t ***out)
{
    entity_t *e;
    brush_t *b;
    entity_t **list = NULL;
    int n = 0;

    for (e = ed.map->entities; e; e = e->next) {
        int hit = 0;
        if (ent_is_point(e))
            hit = e->selected;
        else if (e != ed.map->world)
            for (b = e->brushes; b && !hit; b = b->next)
                hit = b->selected;
        if (hit) {
            list = xrealloc(list, sizeof(entity_t *) * (size_t)(n + 1));
            list[n++] = e;
        }
    }
    *out = list;
    return n;
}

void sel_faces_clear(void)
{
    entity_t *e;
    brush_t *b;
    int i;
    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next)
            for (i = 0; i < b->nfaces; i++)
                b->faces[i].selected = 0;
}

int sel_face_count(void)
{
    entity_t *e;
    brush_t *b;
    int i, n = 0;
    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next)
            for (i = 0; i < b->nfaces; i++)
                n += b->faces[i].selected;
    return n;
}

face_t *sel_first_face(brush_t **owner)
{
    entity_t *e;
    brush_t *b;
    int i;
    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next)
            for (i = 0; i < b->nfaces; i++)
                if (b->faces[i].selected) {
                    if (owner)
                        *owner = b;
                    return &b->faces[i];
                }
    return NULL;
}

void sel_changed(void)
{
    ui_selection_changed();
    ui_redraw_all();
}

/* collects the selected brushes into an array (caller frees) */
static int collect_selected_brushes(brush_t ***out)
{
    entity_t *e;
    brush_t *b;
    brush_t **list = NULL;
    int n = 0;

    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next)
            if (b->selected) {
                list = xrealloc(list, sizeof(brush_t *) * (size_t)(n + 1));
                list[n++] = b;
            }
    *out = list;
    return n;
}

/* removes brush entities that lost all their brushes */
static void remove_empty_entities(void)
{
    entity_t *e, *next;
    for (e = ed.map->entities; e; e = next) {
        next = e->next;
        if (e == ed.map->world || e->brushes)
            continue;
        if (e->ec && e->ec->kind == EC_SOLID) {
            map_unlink_entity(ed.map, e);
            entity_free(e);
        }
    }
}

/* ------------------------------------------------------------------ */
/* interactive transforms                                              */

typedef struct trbrush_s {
    brush_t *b;
    brush_t *orig;
} trbrush_t;

typedef struct trent_s {
    entity_t *e;
    vec3_t origin;
    double angle;
    int has_angle;
} trent_t;

static trbrush_t *trb;
static trent_t *tre;
static int ntrb, ntre, tr_on;

void tr_begin(void)
{
    entity_t *e;
    brush_t *b;

    tr_end(1);
    for (e = ed.map->entities; e; e = e->next) {
        if (ent_is_point(e) && e->selected) {
            const char *a = ent_get(e, "angle");
            tre = xrealloc(tre, sizeof(trent_t) * (size_t)(ntre + 1));
            tre[ntre].e = e;
            v3_copy(e->origin, tre[ntre].origin);
            tre[ntre].has_angle = a && atof(a) >= 0;
            tre[ntre].angle = a ? atof(a) : 0.0;
            ntre++;
        }
        for (b = e->brushes; b; b = b->next)
            if (b->selected) {
                trb = xrealloc(trb, sizeof(trbrush_t) * (size_t)(ntrb + 1));
                trb[ntrb].b = b;
                trb[ntrb].orig = brush_copy(b);
                ntrb++;
            }
    }
    tr_on = 1;
}

int tr_active(void)
{
    return tr_on;
}

static void set_angle(entity_t *e, double deg)
{
    char buf[32];
    deg = fmod(deg, 360.0);
    if (deg < 0)
        deg += 360.0;
    fmt_num(buf, sizeof(buf), round_if_near(deg, 1e-6));
    ent_set(e, "angle", buf);
}

void tr_apply(mat3_t m, const vec3_t t)
{
    int i;
    int ident = mat3_is_identity(m);

    for (i = 0; i < ntrb; i++) {
        brush_copy_faces(trb[i].b, trb[i].orig);
        brush_transform(trb[i].b, m, t, ed.texlock);
    }
    for (i = 0; i < ntre; i++) {
        vec3_t o;
        mat3_mul_vec(m, tre[i].origin, o);
        v3_add(o, t, o);
        ent_set_origin(tre[i].e, o);
        if (!ident && tre[i].has_angle) {
            vec3_t d, d2;
            v3_set(d, cos(DEG2RAD(tre[i].angle)), sin(DEG2RAD(tre[i].angle)), 0);
            mat3_mul_vec(m, d, d2);
            if (fabs(d2[0]) > 1e-6 || fabs(d2[1]) > 1e-6)
                set_angle(tre[i].e, RAD2DEG(atan2(d2[1], d2[0])));
        }
    }
}

void tr_end(int commit)
{
    int i;
    if (!tr_on)
        return;
    for (i = 0; i < ntrb; i++) {
        if (!commit) {
            brush_copy_faces(trb[i].b, trb[i].orig);
            brush_build(trb[i].b);
        }
        brush_free(trb[i].orig);
    }
    for (i = 0; i < ntre; i++)
        if (!commit) {
            ent_set_origin(tre[i].e, tre[i].origin);
            if (tre[i].has_angle)
                set_angle(tre[i].e, tre[i].angle);
        }
    free(trb);
    free(tre);
    trb = NULL;
    tre = NULL;
    ntrb = ntre = 0;
    tr_on = 0;
}

/* ------------------------------------------------------------------ */
/* vertex tool                                                         */

#define VTX_EPS 0.01

static int find_point(const vec3_t *pts, int n, const vec3_t p)
{
    int i;
    for (i = 0; i < n; i++)
        if (v3_equal(pts[i], p, VTX_EPS))
            return i;
    return -1;
}

static void push_point(vec3_t **pts, int *n, int *max, const vec3_t p)
{
    if (*n >= *max) {
        *max = *max ? *max * 2 : 64;
        *pts = xrealloc(*pts, sizeof(vec3_t) * (size_t)*max);
    }
    v3_copy(p, (*pts)[(*n)++]);
}

static void push_handle(vhandle_t **hs, int *n, int *max, int kind, const vec3_t pos, int first, int count)
{
    if (*n >= *max) {
        *max = *max ? *max * 2 : 64;
        *hs = xrealloc(*hs, sizeof(vhandle_t) * (size_t)*max);
    }
    (*hs)[*n].kind = kind;
    v3_copy(pos, (*hs)[*n].pos);
    (*hs)[*n].first = first;
    (*hs)[*n].count = count;
    (*n)++;
}

int vtx_collect(vhandle_t **handles, vec3_t **verts)
{
    vhandle_t *hs = NULL;
    vec3_t *vs = NULL;
    int nh = 0, maxh = 0, nv = 0, maxv = 0, i, k;
    entity_t *e;
    brush_t *b;

    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next) {
            if (!b->selected || !b->valid)
                continue;
            for (i = 0; i < b->nfaces; i++) {
                const winding_t *w = b->faces[i].w;
                vec3_t c;
                if (!w)
                    continue;
                /* vertices (shared between faces and brushes) */
                for (k = 0; k < w->numpoints; k++) {
                    int dup = 0, j;
                    for (j = 0; j < nh && !dup; j++)
                        dup = hs[j].kind == VH_VERTEX && v3_equal(hs[j].pos, w->p[k], VTX_EPS);
                    if (!dup) {
                        push_handle(&hs, &nh, &maxh, VH_VERTEX, w->p[k], nv, 1);
                        push_point(&vs, &nv, &maxv, w->p[k]);
                    }
                }
                /* edges: each one appears in two faces */
                for (k = 0; k < w->numpoints; k++) {
                    const double *a = w->p[k], *bb = w->p[(k + 1) % w->numpoints];
                    vec3_t mid;
                    int dup = 0, j;
                    v3_add(a, bb, mid);
                    v3_scale(mid, 0.5, mid);
                    for (j = 0; j < nh && !dup; j++)
                        dup = hs[j].kind == VH_EDGE && v3_equal(hs[j].pos, mid, VTX_EPS);
                    if (dup)
                        continue;
                    push_handle(&hs, &nh, &maxh, VH_EDGE, mid, nv, 2);
                    push_point(&vs, &nv, &maxv, a);
                    push_point(&vs, &nv, &maxv, bb);
                }
                /* face center */
                winding_center(w, c);
                push_handle(&hs, &nh, &maxh, VH_FACE, c, nv, w->numpoints);
                for (k = 0; k < w->numpoints; k++)
                    push_point(&vs, &nv, &maxv, w->p[k]);
            }
        }
    *handles = hs;
    *verts = vs;
    return nh;
}

int vtx_handle_selected(const vhandle_t *h, const vec3_t *verts)
{
    int i;
    if (!ed.nvsel)
        return 0;
    for (i = 0; i < h->count; i++)
        if (find_point(ed.vsel, ed.nvsel, verts[h->first + i]) < 0)
            return 0;
    return 1;
}

void vtx_clear(void)
{
    free(ed.vsel);
    ed.vsel = NULL;
    ed.nvsel = 0;
}

void vtx_select(const vhandle_t *hs, const int *which, int n, const vec3_t *verts, int mode)
{
    static int maxsel;
    int i, k, all = 1;

    if (mode == SEL_SET) {
        ed.nvsel = 0;
    } else if (mode == SEL_TOGGLE) {
        for (i = 0; i < n && all; i++)
            all = vtx_handle_selected(&hs[which[i]], verts);
        if (all) {
            /* every clicked handle was selected: remove their vertices */
            int o = 0;
            for (k = 0; k < ed.nvsel; k++) {
                int hit = 0, j;
                for (i = 0; i < n && !hit; i++)
                    for (j = 0; j < hs[which[i]].count && !hit; j++)
                        hit = v3_equal(ed.vsel[k], verts[hs[which[i]].first + j], VTX_EPS);
                if (!hit)
                    v3_copy(ed.vsel[k], ed.vsel[o++]);
            }
            ed.nvsel = o;
            return;
        }
    }
    if (!ed.vsel)
        maxsel = 0;
    for (i = 0; i < n; i++)
        for (k = 0; k < hs[which[i]].count; k++) {
            const double *p = verts[hs[which[i]].first + k];
            if (find_point(ed.vsel, ed.nvsel, p) < 0)
                push_point(&ed.vsel, &ed.nvsel, &maxsel, p);
        }
}

/* copies the texturing of the closest original face onto each new face */
static void transfer_textures(brush_t *nb, const brush_t *orig)
{
    int i, j;
    for (i = 0; i < nb->nfaces; i++) {
        face_t *f = &nb->faces[i];
        const face_t *best = NULL;
        double bestdot = -2.0;
        for (j = 0; j < orig->nfaces; j++) {
            const face_t *o = &orig->faces[j];
            double d;
            if (!o->planeok)
                continue;
            if (plane_equal(&f->plane, &o->plane)) {
                best = o;
                break;
            }
            d = v3_dot(f->plane.normal, o->plane.normal);
            if (d > bestdot) {
                bestdot = d;
                best = o;
            }
        }
        if (!best)
            continue;
        str_copy(f->tex, best->tex, sizeof(f->tex));
        f->shift[0] = best->shift[0];
        f->shift[1] = best->shift[1];
        f->rotate = best->rotate;
        f->scale[0] = best->scale[0];
        f->scale[1] = best->scale[1];
        v3_copy(best->uaxis, f->uaxis);
        v3_copy(best->vaxis, f->vaxis);
        f->valve = best->valve;
        if (f->valve)
            face_fix_axes(f);
    }
}

/* the brush with the selected vertices moved, or NULL when none of its
 * vertices is selected; *bad is set when the result is not a valid convex
 * brush containing every moved vertex */
static brush_t *move_vertices(const brush_t *orig, const vec3_t delta, int *bad)
{
    vec3_t *pts = NULL, *moved = NULL;
    int npts = 0, maxpts = 0, nmoved = 0, maxmoved = 0, i, k;
    brush_t *nb;

    *bad = 0;
    for (i = 0; i < orig->nfaces; i++) {
        const winding_t *w = orig->faces[i].w;
        if (!w)
            continue;
        for (k = 0; k < w->numpoints; k++) {
            vec3_t p;
            if (find_point(pts, npts, w->p[k]) >= 0 || find_point(moved, nmoved, w->p[k]) >= 0)
                continue;
            if (find_point(ed.vsel, ed.nvsel, w->p[k]) >= 0) {
                push_point(&moved, &nmoved, &maxmoved, w->p[k]);
                v3_add(w->p[k], delta, p);
            } else {
                v3_copy(w->p[k], p);
            }
            push_point(&pts, &npts, &maxpts, p);
        }
    }
    if (!nmoved) {
        free(pts);
        return NULL;
    }
    nb = brush_from_points(pts, npts, ed_default_texture(), ed.map->valve220);
    if (nb) {
        /* every moved vertex must still be a corner of the new brush,
         * otherwise the move made the brush concave */
        vec3_t *corners = NULL;
        int nc = 0, maxc = 0;
        for (i = 0; i < nb->nfaces; i++)
            for (k = 0; nb->faces[i].w && k < nb->faces[i].w->numpoints; k++)
                if (find_point(corners, nc, nb->faces[i].w->p[k]) < 0)
                    push_point(&corners, &nc, &maxc, nb->faces[i].w->p[k]);
        for (i = 0; i < nmoved && !*bad; i++) {
            vec3_t p;
            v3_add(moved[i], delta, p);
            if (find_point(corners, nc, p) < 0)
                *bad = 1;
        }
        free(corners);
    }
    if (!nb || *bad) {
        brush_free(nb);
        nb = NULL;
        *bad = 1;
    } else {
        transfer_textures(nb, orig);
    }
    free(pts);
    free(moved);
    return nb;
}

void vtx_drag_begin(void)
{
    undo_push("Move Vertices");
    tr_begin();
}

int vtx_drag_apply(const vec3_t delta)
{
    int i, failed = 0;

    for (i = 0; i < ntrb; i++) {
        int bad;
        brush_t *nb;
        brush_copy_faces(trb[i].b, trb[i].orig);
        brush_build(trb[i].b);
        nb = move_vertices(trb[i].orig, delta, &bad);
        if (bad)
            failed++;
        if (nb) {
            brush_copy_faces(trb[i].b, nb);
            brush_free(nb);
        }
    }
    return failed;
}

void vtx_drag_end(int commit, const vec3_t delta)
{
    int i;
    tr_end(commit);
    if (commit)
        for (i = 0; i < ed.nvsel; i++)
            v3_add(ed.vsel[i], delta, ed.vsel[i]);
}

static void transform_selection(const char *desc, mat3_t m, const vec3_t t)
{
    if (!sel_count())
        return;
    undo_push(desc);
    tr_begin();
    tr_apply(m, t);
    tr_end(1);
    sel_changed();
}

/* ------------------------------------------------------------------ */
/* commands                                                            */

void cmd_delete(void)
{
    entity_t *e, *enext;
    brush_t *b, *bnext;

    if (!sel_count())
        return;
    undo_push("Delete");
    for (e = ed.map->entities; e; e = enext) {
        enext = e->next;
        if (ent_is_point(e)) {
            if (e->selected) {
                map_unlink_entity(ed.map, e);
                entity_free(e);
            }
            continue;
        }
        for (b = e->brushes; b; b = bnext) {
            bnext = b->next;
            if (b->selected) {
                ent_unlink_brush(e, b);
                brush_free(b);
            }
        }
        if (e != ed.map->world && !e->brushes) {
            map_unlink_entity(ed.map, e);
            entity_free(e);
        }
    }
    sel_changed();
}

void cmd_copy(void)
{
    if (!sel_count())
        return;
    free(ed.clipboard);
    ed.clipboard = map_write_string(ed.map, 1);
    ui_status("Copied %d object(s)", sel_count());
}

void cmd_cut(void)
{
    if (!sel_count())
        return;
    cmd_copy();
    cmd_delete();
}

static int paste_text(const char *text, const vec3_t offset)
{
    char err[256];
    map_t *src = map_parse(text, err, sizeof(err));
    entity_t *e, *enext;
    brush_t *b;
    int moved = offset && (offset[0] || offset[1] || offset[2]);

    if (!src) {
        log_warn("paste: %s", err);
        return 0;
    }
    sel_clear();
    for (e = src->entities; e; e = enext) {
        enext = e->next;
        if (e == src->world) {
            while ((b = e->brushes) != NULL) {
                ent_unlink_brush(e, b);
                if (moved)
                    brush_translate(b, offset, ed.texlock);
                b->selected = 1;
                ent_add_brush(ed.map->world, b);
            }
            continue;
        }
        map_unlink_entity(src, e);
        if (moved) {
            if (e->brushes) {
                for (b = e->brushes; b; b = b->next)
                    brush_translate(b, offset, ed.texlock);
            } else {
                vec3_t o;
                v3_add(e->origin, offset, o);
                ent_set_origin(e, o);
            }
        }
        for (b = e->brushes; b; b = b->next)
            b->selected = 1;
        e->selected = e->brushes == NULL;
        map_add_entity(ed.map, e);
        eclass_bind(e);
    }
    map_free(src);
    return 1;
}

void cmd_paste(void)
{
    if (!ed.clipboard)
        return;
    undo_push("Paste");
    paste_text(ed.clipboard, NULL);
    sel_changed();
}

void sel_duplicate_in_place(void)
{
    char *text = map_write_string(ed.map, 1);
    paste_text(text, NULL);
    free(text);
}

void cmd_duplicate(int offset)
{
    char *text;
    vec3_t off;

    if (!sel_count())
        return;
    v3_set(off, offset ? ed.grid : 0, offset ? ed.grid : 0, 0);
    text = map_write_string(ed.map, 1);
    undo_push("Duplicate");
    paste_text(text, off);
    free(text);
    sel_changed();
}

void cmd_translate(const vec3_t d)
{
    mat3_t m;
    mat3_identity(m);
    transform_selection("Move", m, d);
}

void cmd_flip(int axis)
{
    vec3_t mins, maxs, t;
    mat3_t m;

    if (!sel_bounds(mins, maxs))
        return;
    mat3_identity(m);
    m[axis][axis] = -1.0;
    v3_clear(t);
    t[axis] = mins[axis] + maxs[axis];
    transform_selection("Flip", m, t);
}

void cmd_rotate(int axis, double degrees)
{
    vec3_t mins, maxs, c, mc, t;
    mat3_t m;
    int i;

    if (!sel_bounds(mins, maxs))
        return;
    for (i = 0; i < 3; i++)
        c[i] = (mins[i] + maxs[i]) * 0.5;
    mat3_rotation(m, axis, degrees);
    mat3_mul_vec(m, c, mc);
    v3_sub(c, mc, t);
    if (fmod(fabs(degrees), 90.0) < 1e-9)
        for (i = 0; i < 3; i++)
            t[i] = floor(t[i] + 0.5);
    transform_selection("Rotate", m, t);
}

void cmd_snap_selection(void)
{
    entity_t *e;
    brush_t *b;
    int failed = 0;

    if (!sel_count())
        return;
    undo_push("Snap to Grid");
    for (e = ed.map->entities; e; e = e->next) {
        if (ent_is_point(e) && e->selected) {
            vec3_t o;
            int i;
            for (i = 0; i < 3; i++)
                o[i] = snap_value(e->origin[i], ed.grid);
            ent_set_origin(e, o);
        }
        for (b = e->brushes; b; b = b->next)
            if (b->selected && !brush_snap(b, ed.grid))
                failed++;
    }
    if (failed)
        ui_status("%d brush(es) could not be snapped without becoming invalid", failed);
    sel_changed();
}

static const char *default_solid_class(void)
{
    int i;
    if (eclass_find("func_wall"))
        return "func_wall";
    for (i = 0; i < eclass_count(); i++) {
        eclass_t *ec = eclass_get(i);
        if (ec->kind == EC_SOLID && !str_ieq(ec->name, "worldspawn"))
            return ec->name;
    }
    return "func_wall";
}

void cmd_tie_to_entity(const char *classname)
{
    brush_t **list;
    int n, i;
    eclass_t *ec = classname ? eclass_find(classname) : NULL;
    entity_t *ne;

    n = collect_selected_brushes(&list);
    if (!n) {
        ui_message("Select one or more brushes to tie to an entity.");
        return;
    }
    if (!ec || ec->kind != EC_SOLID || str_ieq(ec->name, "worldspawn"))
        classname = default_solid_class();
    undo_push("Tie to Entity");
    ne = entity_new();
    ent_set(ne, "classname", classname);
    map_add_entity(ed.map, ne);
    for (i = 0; i < n; i++) {
        ent_unlink_brush(list[i]->owner, list[i]);
        ent_add_brush(ne, list[i]);
    }
    free(list);
    eclass_bind(ne);
    remove_empty_entities();
    sel_changed();
    ui_open_entity_dialog();
}

void cmd_move_to_world(void)
{
    brush_t **list;
    int n, i, moved = 0;

    n = collect_selected_brushes(&list);
    for (i = 0; i < n; i++)
        if (list[i]->owner != ed.map->world)
            moved++;
    if (!moved) {
        free(list);
        return;
    }
    undo_push("Move to World");
    for (i = 0; i < n; i++) {
        if (list[i]->owner == ed.map->world)
            continue;
        ent_unlink_brush(list[i]->owner, list[i]);
        ent_add_brush(ed.map->world, list[i]);
    }
    free(list);
    remove_empty_entities();
    sel_changed();
}

typedef struct pending_s {
    entity_t *e;
    brush_t *b;
} pending_t;

void cmd_carve(void)
{
    brush_t **carvers;
    int ncarvers, changed = 0, npend = 0, i, j, k;
    pending_t *pend = NULL;
    entity_t *e;
    brush_t *b, *bnext;

    ncarvers = collect_selected_brushes(&carvers);
    if (!ncarvers) {
        ui_message("Select the brush(es) to carve with.");
        return;
    }
    for (e = ed.map->entities; e; e = e->next) {
        for (b = e->brushes; b; b = bnext) {
            brush_t **pieces;
            int npieces = 1, modified = 0;

            bnext = b->next;
            if (b->selected || !b->valid)
                continue;
            pieces = xmalloc(sizeof(brush_t *));
            pieces[0] = b;
            for (i = 0; i < ncarvers; i++) {
                brush_t **next = NULL;
                int nnext = 0;
                for (j = 0; j < npieces; j++) {
                    brush_t **out;
                    int n = brush_subtract(pieces[j], carvers[i], &out, ed.map->valve220);
                    if (n < 0) {
                        next = xrealloc(next, sizeof(brush_t *) * (size_t)(nnext + 1));
                        next[nnext++] = pieces[j];
                        continue;
                    }
                    modified = 1;
                    if (n) {
                        next = xrealloc(next, sizeof(brush_t *) * (size_t)(nnext + n));
                        for (k = 0; k < n; k++)
                            next[nnext++] = out[k];
                    }
                    free(out);
                    if (pieces[j] != b)
                        brush_free(pieces[j]);
                }
                free(pieces);
                pieces = next;
                npieces = nnext;
            }
            if (modified) {
                if (!changed)
                    undo_push("Carve");
                changed = 1;
                pend = xrealloc(pend, sizeof(pending_t) * (size_t)(npend + npieces));
                for (j = 0; j < npieces; j++) {
                    pend[npend].e = e;
                    pend[npend].b = pieces[j];
                    npend++;
                }
                ent_unlink_brush(e, b);
                brush_free(b);
            }
            free(pieces);
        }
    }
    for (i = 0; i < npend; i++)
        ent_add_brush(pend[i].e, pend[i].b);
    free(pend);
    free(carvers);
    if (changed) {
        remove_empty_entities();
        ui_status("Carved into %d piece(s)", npend);
        sel_changed();
    } else {
        ui_status("Nothing to carve: the selection does not intersect other brushes");
    }
}

void cmd_hollow(double thickness)
{
    brush_t **list;
    int n, i, j;

    if (thickness <= 0)
        return;
    n = collect_selected_brushes(&list);
    if (!n) {
        free(list);
        return;
    }
    undo_push("Hollow");
    for (i = 0; i < n; i++) {
        brush_t **walls;
        entity_t *owner = list[i]->owner;
        int nw = brush_hollow(list[i], thickness, &walls, ed.map->valve220);
        if (!nw) {
            free(walls);
            continue;
        }
        for (j = 0; j < nw; j++) {
            walls[j]->selected = 1;
            ent_add_brush(owner, walls[j]);
        }
        free(walls);
        ent_unlink_brush(owner, list[i]);
        brush_free(list[i]);
    }
    free(list);
    sel_changed();
}

void cmd_merge(void)
{
    brush_t **list, *nb;
    vec3_t *pts = NULL;
    int n, npts = 0, i, j, k, l;
    entity_t *owner;

    n = collect_selected_brushes(&list);
    if (n < 2) {
        free(list);
        ui_message("Select two or more brushes to merge (convex hull).");
        return;
    }
    for (i = 0; i < n; i++)
        for (j = 0; j < list[i]->nfaces; j++) {
            const winding_t *w = list[i]->faces[j].w;
            if (!w)
                continue;
            for (k = 0; k < w->numpoints; k++) {
                int dup = 0;
                for (l = 0; l < npts && !dup; l++)
                    dup = v3_equal(pts[l], w->p[k], 0.01);
                if (dup)
                    continue;
                pts = xrealloc(pts, sizeof(vec3_t) * (size_t)(npts + 1));
                v3_copy(w->p[k], pts[npts++]);
            }
        }
    nb = brush_from_points(pts, npts, ed_default_texture(), ed.map->valve220);
    free(pts);
    if (!nb) {
        free(list);
        ui_message("Merge failed.");
        return;
    }
    /* keep the texturing of faces that survive the merge */
    for (i = 0; i < nb->nfaces; i++) {
        face_t *f = &nb->faces[i];
        int done = 0;
        for (j = 0; j < n && !done; j++)
            for (k = 0; k < list[j]->nfaces && !done; k++) {
                const face_t *o = &list[j]->faces[k];
                if (!o->planeok || !plane_equal(&f->plane, &o->plane))
                    continue;
                str_copy(f->tex, o->tex, sizeof(f->tex));
                f->shift[0] = o->shift[0];
                f->shift[1] = o->shift[1];
                f->rotate = o->rotate;
                f->scale[0] = o->scale[0];
                f->scale[1] = o->scale[1];
                v3_copy(o->uaxis, f->uaxis);
                v3_copy(o->vaxis, f->vaxis);
                f->valve = o->valve;
                done = 1;
            }
    }
    undo_push("Merge");
    owner = list[0]->owner;
    for (i = 0; i < n; i++) {
        ent_unlink_brush(list[i]->owner, list[i]);
        brush_free(list[i]);
    }
    free(list);
    nb->selected = 1;
    ent_add_brush(owner, nb);
    remove_empty_entities();
    sel_changed();
}

int clip_plane(plane_t *pl)
{
    vec3_t dir, axis;

    if (ed.clip_npts < 2)
        return 0;
    v3_sub(ed.clip_pts[1], ed.clip_pts[0], dir);
    v3_clear(axis);
    axis[ed.clip_axis] = 1.0;
    v3_cross(dir, axis, pl->normal);
    if (v3_normalize(pl->normal) < 1e-6)
        return 0;
    pl->dist = v3_dot(pl->normal, ed.clip_pts[0]);
    return 1;
}

void cmd_clip_apply(void)
{
    plane_t pl;
    brush_t **list;
    int n, i, changed = 0;

    if (!clip_plane(&pl)) {
        ui_status("Clip: drag a line in a 2D view first");
        return;
    }
    n = collect_selected_brushes(&list);
    for (i = 0; i < n; i++) {
        brush_t *front = NULL, *back = NULL;
        entity_t *owner = list[i]->owner;

        brush_split(list[i], &pl, &front, &back, NULL, ed_default_texture(), ed.map->valve220);
        if (!front || !back) {
            brush_free(front);
            brush_free(back);
            continue;
        }
        if (!changed)
            undo_push("Clip");
        changed = 1;
        if (ed.clip_mode == CLIP_BACK) {
            brush_free(front);
            front = NULL;
        }
        if (ed.clip_mode == CLIP_FRONT) {
            brush_free(back);
            back = NULL;
        }
        if (front) {
            front->selected = 1;
            ent_add_brush(owner, front);
        }
        if (back) {
            back->selected = 1;
            ent_add_brush(owner, back);
        }
        ent_unlink_brush(owner, list[i]);
        brush_free(list[i]);
    }
    free(list);
    ed.clip_npts = 0;
    if (!changed)
        ui_status("Clip: the plane does not cross any selected brush");
    sel_changed();
}

void cmd_apply_texture(void)
{
    entity_t *e;
    brush_t *b;
    int i, faces = sel_face_count();

    if (!faces && !sel_brush_count())
        return;
    undo_push("Apply Texture");
    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next)
            for (i = 0; i < b->nfaces; i++)
                if (faces ? b->faces[i].selected : b->selected)
                    str_copy(b->faces[i].tex, ed_default_texture(), sizeof(b->faces[i].tex));
    sel_changed();
}

void cmd_create_brush(const vec3_t mins, const vec3_t maxs, int hax, int vax, int dax)
{
    brush_t *b = brush_primitive(ed.prim, ed.prim_sides, hax, vax, dax, mins, maxs,
                                 ed_default_texture(), ed.map->valve220);
    if (!b) {
        ui_status("Cannot create a brush with that size");
        return;
    }
    undo_push("Create Brush");
    sel_clear();
    b->selected = 1;
    ent_add_brush(ed.map->world, b);
    v3_copy(mins, ed.last_block_mins);
    v3_copy(maxs, ed.last_block_maxs);
    ed.have_last_block = 1;
    sel_changed();
}

entity_t *cmd_create_entity(const char *classname, const vec3_t origin)
{
    entity_t *e;

    if (!classname || !*classname)
        return NULL;
    undo_push("Create Entity");
    e = entity_new();
    ent_set(e, "classname", classname);
    ent_set_origin(e, origin);
    map_add_entity(ed.map, e);
    eclass_bind(e);
    sel_clear();
    e->selected = 1;
    sel_changed();
    return e;
}

void cmd_convert_valve(void)
{
    entity_t *e;
    brush_t *b;
    int i;

    if (ed.map->valve220) {
        ui_message("The map already uses the Valve 220 format.");
        return;
    }
    undo_push("Convert to Valve 220");
    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next)
            for (i = 0; i < b->nfaces; i++)
                face_to_valve(&b->faces[i]);
    ed.map->valve220 = 1;
    ent_set(ed.map->world, "mapversion", "220");
    ui_map_changed();
    ui_redraw_all();
}

int cmd_goto_brush(int entity_index, int brush_index)
{
    entity_t *e;
    brush_t *b;
    int ei = 0, bi;

    for (e = ed.map->entities; e; e = e->next, ei++) {
        if (ei != entity_index)
            continue;
        if (ent_is_point(e) || brush_index < 0) {
            vec3_t mins, maxs, c;
            if (ent_is_point(e))
                sel_entity(e, SEL_SET);
            ent_bounds(e, mins, maxs);
            v3_add(mins, maxs, c);
            v3_scale(c, 0.5, c);
            views_center_2d(c);
            camera_look_at(c, 256);
            sel_changed();
            return 1;
        }
        for (b = e->brushes, bi = 0; b; b = b->next, bi++) {
            if (bi == brush_index) {
                vec3_t c;
                sel_clear();
                b->selected = 1;
                v3_add(b->mins, b->maxs, c);
                v3_scale(c, 0.5, c);
                views_center_2d(c);
                camera_look_at(c, 256);
                sel_changed();
                return 1;
            }
        }
    }
    return 0;
}

void cmd_map_info(char *buf, size_t size)
{
    entity_t *e;
    brush_t *b;
    int nent = 0, npoint = 0, nsolid = 0, nbrush = 0, nfaces = 0, nworld = 0, i;
    vec3_t mins, maxs;

    bounds_clear(mins, maxs);
    for (e = ed.map->entities; e; e = e->next) {
        nent++;
        if (ent_is_point(e))
            npoint++;
        else if (e != ed.map->world)
            nsolid++;
        for (b = e->brushes; b; b = b->next) {
            nbrush++;
            if (e == ed.map->world)
                nworld++;
            for (i = 0; i < b->nfaces; i++)
                nfaces++;
            if (b->valid) {
                bounds_add(mins, maxs, b->mins);
                bounds_add(mins, maxs, b->maxs);
            }
        }
    }
    if (!bounds_valid(mins, maxs)) {
        v3_clear(mins);
        v3_clear(maxs);
    }
    snprintf(buf, size,
             "File: %s\nFormat: %s\nGame: %s\n\n"
             "Entities: %d (%d point, %d brush)\n"
             "Brushes: %d (%d world)\nFaces: %d\n\n"
             "Bounds: (%g %g %g) - (%g %g %g)\n"
             "Textures loaded: %d",
             ed.map->path[0] ? ed.map->path : "(unsaved)",
             ed.map->valve220 ? "Valve 220" : "Standard (Quake)", map_game_name,
             nent, npoint, nsolid, nbrush, nworld, nfaces,
             mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2], tex_count());
}

void cmd_check_map(void)
{
    entity_t *e;
    brush_t *b;
    int ei = 0, problems = 0, missing_tex = 0, i;
    int nplayer = 0;

    sel_clear();
    for (e = ed.map->entities; e; e = e->next, ei++) {
        const char *cn = ent_classname(e);
        int bi = 0;
        if (!*cn) {
            log_warn("check: entity %d has no classname", ei);
            problems++;
        } else if (!eclass_find(cn) && eclass_count()) {
            log_warn("check: entity %d: unknown class '%s'", ei, cn);
            problems++;
        }
        if (str_ieq(cn, "info_player_start"))
            nplayer++;
        if (e->ec && e->ec->kind == EC_SOLID && e != ed.map->world && !e->brushes) {
            log_warn("check: brush entity %d (%s) has no brushes", ei, cn);
            problems++;
        }
        if (e->ec && e->ec->kind == EC_POINT && e->brushes && eclass_find(cn)) {
            log_warn("check: point entity %d (%s) has brushes", ei, cn);
            problems++;
        }
        for (b = e->brushes; b; b = b->next, bi++) {
            if (!b->valid) {
                log_warn("check: entity %d brush %d is invalid", ei, bi);
                b->selected = 1;
                problems++;
            }
            for (i = 0; i < b->nfaces; i++)
                if (tex_count() && !tex_find(b->faces[i].tex))
                    missing_tex++;
        }
    }
    if (!nplayer) {
        log_warn("check: no info_player_start");
        problems++;
    }
    if (missing_tex)
        log_warn("check: %d face(s) use textures that are not loaded", missing_tex);
    if (problems || missing_tex) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%d problem(s) found, %d face(s) with missing textures.\n"
                 "See the console for details; invalid brushes were selected.", problems, missing_tex);
        ui_message(msg);
    } else {
        ui_message("No problems found.");
    }
    sel_changed();
}

/* ------------------------------------------------------------------ */
/* picking                                                             */

int pick_ray(const vec3_t org, const vec3_t dir, pick_t *out)
{
    entity_t *e;
    brush_t *b;
    double best = 1e30;
    int found = 0;

    for (e = ed.map->entities; e; e = e->next) {
        if (ent_is_point(e)) {
            vec3_t mins, maxs;
            double t;
            ent_bounds(e, mins, maxs);
            if (bbox_ray(mins, maxs, org, dir, &t) && t < best) {
                best = t;
                out->ent = e;
                out->brush = NULL;
                out->face = -1;
                found = 1;
            }
            continue;
        }
        for (b = e->brushes; b; b = b->next) {
            double t;
            int f;
            if (b->valid && brush_ray(b, org, dir, &t, &f) && t < best) {
                best = t;
                out->ent = e;
                out->brush = b;
                out->face = f;
                found = 1;
            }
        }
    }
    if (found) {
        out->t = best;
        v3_ma(org, best, dir, out->point);
    }
    return found;
}

int pick_2d(int hax, int vax, int dax, double h, double v, pick_t *out)
{
    entity_t *e;
    brush_t *b;
    double best = 1e30;
    int found = 0;
    vec3_t org, dir;

    v3_clear(org);
    v3_clear(dir);
    org[hax] = h;
    org[vax] = v;
    org[dax] = -1e6;
    dir[dax] = 1.0;

    for (e = ed.map->entities; e; e = e->next) {
        if (ent_is_point(e)) {
            vec3_t mins, maxs;
            double area;
            ent_bounds(e, mins, maxs);
            if (h < mins[hax] || h > maxs[hax] || v < mins[vax] || v > maxs[vax])
                continue;
            area = (maxs[hax] - mins[hax]) * (maxs[vax] - mins[vax]) * 0.5;
            if (area < best) {
                best = area;
                out->ent = e;
                out->brush = NULL;
                out->face = -1;
                found = 1;
            }
            continue;
        }
        for (b = e->brushes; b; b = b->next) {
            double t, area;
            int f;
            if (!b->valid || h < b->mins[hax] || h > b->maxs[hax] || v < b->mins[vax] || v > b->maxs[vax])
                continue;
            if (!brush_ray(b, org, dir, &t, &f))
                continue;
            area = (b->maxs[hax] - b->mins[hax]) * (b->maxs[vax] - b->mins[vax]);
            if (area < best) {
                best = area;
                out->ent = e;
                out->brush = b;
                out->face = f;
                found = 1;
            }
        }
    }
    return found;
}
