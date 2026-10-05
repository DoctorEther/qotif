/*
 * actions.c - named editor actions and their configurable key bindings
 */
#include "actions.h"
#include "editor.h"
#include "game.h"
#include "ui.h"
#include "view.h"

#include <ctype.h>

/* ------------------------------------------------------------------ */
/* action implementations                                              */

static int busy(void)
{
    int i;
    for (i = 0; i < MAX_VIEWS; i++)
        if (views[i].mode != MODE_NONE)
            return 1;
    return tr_active();
}

static void a_new(void)
{
    if (ui_ask_save())
        ed_new_map();
}

static void a_open(void)
{
    char path[PATH_LEN];
    if (!ui_ask_save())
        return;
    if (ui_file_dialog("Open Map", "*.map", 0, path, sizeof(path)))
        ed_load_map(path);
}

static void a_saveas(void)
{
    char path[PATH_LEN];
    if (!ui_file_dialog("Save Map As", "*.map", 1, path, sizeof(path)))
        return;
    if (!*path_ext(path) && strlen(path) + 4 < sizeof(path))
        strcat(path, ".map");
    ed_save_map(path);
}

static void a_save(void)
{
    if (!ed.map->path[0])
        a_saveas();
    else
        ed_save_map(ed.map->path);
}

static void a_pointfile_load(void)
{
    char path[PATH_LEN + 8];
    if (ed.map->path[0]) {
        char base[PATH_LEN];
        path_strip_ext(base, sizeof(base), ed.map->path);
        snprintf(path, sizeof(path), "%s.pts", base);
        if (file_exists(path)) {
            ed_load_pointfile(path);
            return;
        }
        snprintf(path, sizeof(path), "%s.lin", base);
        if (file_exists(path)) {
            ed_load_pointfile(path);
            return;
        }
    }
    if (ui_file_dialog("Load Point File", "*.pts", 0, path, sizeof(path)))
        ed_load_pointfile(path);
}

static void a_pointfile_clear(void) { ed_clear_pointfile(); }
static void a_compile(void) { ui_open_compile_dialog(); }

static void a_quit(void)
{
    if (ui_ask_save())
        ui_quit();
}

static void a_undo(void)
{
    if (!undo_undo())
        ui_status("Nothing to undo");
}

static void a_redo(void)
{
    if (!undo_redo())
        ui_status("Nothing to redo");
}

static void a_cut(void) { cmd_cut(); }
static void a_copy(void) { cmd_copy(); }
static void a_paste(void) { cmd_paste(); }
static void a_duplicate(void) { cmd_duplicate(1); }
static void a_delete(void) { cmd_delete(); }

static void a_select_all(void)
{
    sel_all();
    sel_changed();
}

static void a_select_none(void)
{
    sel_clear();
    sel_changed();
}

static void a_select_invert(void)
{
    sel_invert();
    sel_changed();
}

static void a_cancel(void)
{
    if (view_cancel())
        return;
    if (sel_face_count())
        sel_faces_clear();
    else
        sel_clear();
    sel_changed();
}

static void a_properties(void) { ui_open_entity_dialog(); }
static void a_face_edit(void) { ui_open_face_dialog(); }
static void a_preferences(void) { ui_open_prefs_dialog(); }

static void a_grid_smaller(void) { ed_set_grid(ed.grid / 2); }
static void a_grid_larger(void) { ed_set_grid(ed.grid * 2); }

static void a_redraw(void)
{
    prefs.show_grid = ed.show_grid;
    prefs.snap = ed.snap;
    prefs.texlock = ed.texlock;
    ui_redraw_all();
}

static void a_center_2d(void)
{
    vec3_t mins, maxs, c;
    if (!sel_bounds(mins, maxs))
        return;
    v3_add(mins, maxs, c);
    v3_scale(c, 0.5, c);
    views_center_2d(c);
}

static void a_center_3d(void)
{
    vec3_t mins, maxs, c, d;
    if (!sel_bounds(mins, maxs))
        return;
    v3_add(mins, maxs, c);
    v3_scale(c, 0.5, c);
    v3_sub(maxs, mins, d);
    camera_look_at(c, v3_len(d) + 64);
    ui_redraw_all();
}

static void a_layout_four(void) { prefs.layout = LAYOUT_FOUR; ui_layout_changed(); }
static void a_layout_two(void) { prefs.layout = LAYOUT_TWO; ui_layout_changed(); }
static void a_layout_one(void) { prefs.layout = LAYOUT_ONE; ui_layout_changed(); }
static void a_view_maximize(void) { ui_maximize_view(active_view); }
static void a_console(void) { ui_console_changed(); }

static void a_capture(void)
{
    view_t *v = active_view;
    int i;

    if (captured_view) {
        view_set_capture(NULL, 0);
        return;
    }
    for (i = 0; (!v || v->type != VIEW_3D) && i < MAX_VIEWS; i++)
        if (views[i].type == VIEW_3D)
            v = &views[i];
    if (!v || v->type != VIEW_3D) {
        ui_status("No 3D view to capture the mouse in");
        return;
    }
    view_set_capture(v, 1);
}

static void a_render_cycle(void)
{
    int i;
    for (i = 0; i < MAX_VIEWS; i++)
        if (views[i].type == VIEW_3D && (!active_view || active_view->type != VIEW_3D || active_view == &views[i]))
            views[i].rmode = (rendermode_t)((views[i].rmode + 1) % 3);
    ui_redraw_all();
}

static void a_texture_browser(void) { ui_open_texture_browser(); }

static void a_tool_select(void) { ed_set_tool(TOOL_SELECT); }
static void a_tool_camera(void) { ed_set_tool(TOOL_CAMERA); }
static void a_tool_entity(void) { ed_set_tool(TOOL_ENTITY); }
static void a_tool_block(void) { ed_set_tool(TOOL_BLOCK); }
static void a_tool_texture(void) { ed_set_tool(TOOL_TEXTURE); }
static void a_tool_vertex(void) { ed_set_tool(TOOL_VERTEX); }

static void a_tool_clip(void)
{
    /* pressing the clip shortcut again cycles the kept side (Hammer) */
    if (ed.tool == TOOL_CLIP) {
        static const char *names[] = { "both sides", "front side", "back side" };
        ed.clip_mode = (ed.clip_mode + 1) % 3;
        ui_status("Clip keeps %s", names[ed.clip_mode]);
        ui_redraw_all();
        return;
    }
    ed_set_tool(TOOL_CLIP);
}

static void a_clip_apply(void)
{
    if (ed.tool == TOOL_CLIP)
        cmd_clip_apply();
}

static void a_clip_cycle(void)
{
    if (ed.tool == TOOL_CLIP)
        a_tool_clip();
}

static void a_apply_texture(void) { cmd_apply_texture(); }

static void a_tie(void) { cmd_tie_to_entity(ed.entclass); }
static void a_to_world(void) { cmd_move_to_world(); }
static void a_carve(void) { cmd_carve(); }
static void a_merge(void) { cmd_merge(); }

static void a_hollow(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", ed.grid);
    if (!sel_brush_count()) {
        ui_status("Select brushes to hollow");
        return;
    }
    if (ui_prompt("Hollow", "Wall thickness:", buf, sizeof(buf)))
        cmd_hollow(atof(buf));
}

static view_t *view_or_top(void)
{
    return active_view;
}

static void a_flip_h(void)
{
    view_t *v = view_or_top();
    cmd_flip(v && v->type != VIEW_3D ? v->hax : 0);
}

static void a_flip_v(void)
{
    view_t *v = view_or_top();
    cmd_flip(v && v->type != VIEW_3D ? v->vax : 2);
}

static double view_sign(const view_t *v)
{
    vec3_t eh, ev, c;
    if (!v || v->type == VIEW_3D)
        return 1.0;
    v3_clear(eh);
    v3_clear(ev);
    eh[v->hax] = 1;
    ev[v->vax] = 1;
    v3_cross(eh, ev, c);
    return c[v->dax] >= 0 ? 1.0 : -1.0;
}

static void a_rotate_cw(void)
{
    view_t *v = view_or_top();
    cmd_rotate(v && v->type != VIEW_3D ? v->dax : 2, -90.0 * view_sign(v));
}

static void a_rotate_ccw(void)
{
    view_t *v = view_or_top();
    cmd_rotate(v && v->type != VIEW_3D ? v->dax : 2, 90.0 * view_sign(v));
}

static void a_snap_selected(void) { cmd_snap_selection(); }

static void a_goto_brush(void)
{
    char buf[64] = "0 0";
    int e = 0, b = -1;
    if (!ui_prompt("Go to Brush", "Entity and brush number (e.g. \"0 12\"):", buf, sizeof(buf)))
        return;
    if (sscanf(buf, "%d %d", &e, &b) < 1)
        return;
    if (!cmd_goto_brush(e, b))
        ui_message("No such entity or brush.");
}

static void a_map_info(void)
{
    char buf[1024];
    cmd_map_info(buf, sizeof(buf));
    ui_message(buf);
}

static void a_check_map(void) { cmd_check_map(); }
static void a_convert_valve(void) { cmd_convert_valve(); }
static void a_reload(void) { ed_load_resources(); }

static void a_set_wads(void)
{
    char buf[1024];
    const char *cur = ent_get(ed.map->world, "wad");
    str_copy(buf, cur ? cur : "", sizeof(buf));
    if (!ui_prompt("Texture WADs", "WAD files (separated by ';'):", buf, sizeof(buf)))
        return;
    undo_push("Set WADs");
    if (*buf)
        ent_set(ed.map->world, "wad", buf);
    else
        ent_remove(ed.map->world, "wad");
    ed_load_resources();
}

static void a_entity_defs(void)
{
    char path[PATH_LEN], value[PATH_LEN + 16];
    if (!ui_file_dialog("Entity Definitions", "*.[fd][ge][df]", 0, path, sizeof(path)))
        return;
    undo_push("Set Entity Definitions");
    snprintf(value, sizeof(value), "external:%s", path);
    ent_set(ed.map->world, "_tb_def", value);
    ed_load_resources();
}

static void nudge(int dh, int dv, int dd)
{
    view_t *v = active_view;
    vec3_t d;

    v3_clear(d);
    if (!v || v->type == VIEW_3D) {
        double yaw = DEG2RAD(ed.cam.yaw);
        double fx = cos(yaw), fy = sin(yaw);
        if (fabs(fx) >= fabs(fy)) {
            d[0] += dv * (fx > 0 ? 1 : -1);
            d[1] += dh * (fx > 0 ? -1 : 1);
        } else {
            d[1] += dv * (fy > 0 ? 1 : -1);
            d[0] += dh * (fy > 0 ? 1 : -1);
        }
        d[2] += dd;
    } else {
        d[v->hax] = dh;
        d[v->vax] = dv;
        d[v->dax] = dd;
    }
    if (!sel_count()) {
        if (v && v->type != VIEW_3D) {
            v->cx += dh * 64 / v->zoom;
            v->cy += dv * 64 / v->zoom;
            ui_redraw_view(v);
        }
        return;
    }
    v3_scale(d, ed.grid, d);
    cmd_translate(d);
}

static void a_nudge_left(void) { nudge(-1, 0, 0); }
static void a_nudge_right(void) { nudge(1, 0, 0); }
static void a_nudge_up(void) { nudge(0, 1, 0); }
static void a_nudge_down(void) { nudge(0, -1, 0); }
static void a_nudge_in(void) { nudge(0, 0, 1); }
static void a_nudge_out(void) { nudge(0, 0, -1); }

static void a_keys_help(void) { ui_open_keys_help(); }

static void a_about(void)
{
    ui_message(QOTIF_NAME " " QOTIF_VERSION "\n\n"
               "A Quake map editor for X11/Motif and OpenGL 1.x.\n"
               "Hammer-style interface, TrenchBroom-style configuration:\n"
               "game configs, FGD/DEF entity definitions, key bindings,\n"
               "colors and compile profiles are all plain text files.");
}

/* ------------------------------------------------------------------ */
/* table                                                               */

#define A(name, label, keys, fn) { name, label, keys, fn, NULL, 0, { { 0, "", 0 } }, 0, NULL }
#define T(name, label, keys, fn, var) { name, label, keys, fn, var, 0, { { 0, "", 0 } }, 0, NULL }
#define F(name, label, keys, bit) { name, label, keys, NULL, NULL, bit, { { 0, "", 0 } }, 0, NULL }

action_t actions[] = {
    A("new",              "New",                      "Ctrl+N",        a_new),
    A("open",             "Open...",                  "Ctrl+O",        a_open),
    A("save",             "Save",                     "Ctrl+S",        a_save),
    A("save_as",          "Save As...",               "Ctrl+Shift+S",  a_saveas),
    A("pointfile_load",   "Load Point File",          "",              a_pointfile_load),
    A("pointfile_clear",  "Clear Point File",         "",              a_pointfile_clear),
    A("compile",          "Compile / Run...",         "F9",            a_compile),
    A("quit",             "Exit",                     "Ctrl+Q",        a_quit),

    A("undo",             "Undo",                     "Ctrl+Z",        a_undo),
    A("redo",             "Redo",                     "Ctrl+Y, Ctrl+Shift+Z", a_redo),
    A("cut",              "Cut",                      "Ctrl+X",        a_cut),
    A("copy",             "Copy",                     "Ctrl+C",        a_copy),
    A("paste",            "Paste",                    "Ctrl+V",        a_paste),
    A("duplicate",        "Duplicate",                "Ctrl+D",        a_duplicate),
    A("delete",           "Delete",                   "Delete",        a_delete),
    A("select_all",       "Select All",               "Ctrl+A",        a_select_all),
    A("select_none",      "Select None",              "Ctrl+Shift+A",  a_select_none),
    A("select_invert",    "Invert Selection",         "Ctrl+I",        a_select_invert),
    A("cancel",           "Cancel / Deselect",        "Escape",        a_cancel),
    A("properties",       "Object Properties...",     "Alt+Return",    a_properties),
    A("face_edit",        "Face Properties...",       "",              a_face_edit),
    A("preferences",      "Preferences...",           "Ctrl+comma",    a_preferences),

    A("grid_smaller",     "Smaller Grid",             "bracketleft",   a_grid_smaller),
    A("grid_larger",      "Larger Grid",              "bracketright",  a_grid_larger),
    T("toggle_grid",      "Show Grid",                "Shift+G",       a_redraw, &ed.show_grid),
    T("toggle_snap",      "Snap to Grid",             "Shift+W",       a_redraw, &ed.snap),
    A("center_2d",        "Center 2D Views on Selection", "Ctrl+E",    a_center_2d),
    A("center_3d",        "Center Camera on Selection", "Ctrl+Shift+E", a_center_3d),
    A("layout_four",      "Four Views",               "",              a_layout_four),
    A("layout_two",       "Two Views",                "",              a_layout_two),
    A("layout_one",       "Single View",              "",              a_layout_one),
    A("view_maximize",    "Maximize Current View",    "Shift+space",   a_view_maximize),
    A("capture_mouse",    "Capture Mouse (3D View)",  "Z",             a_capture),
    A("render_cycle",     "Cycle 3D Render Mode",     "Ctrl+R",        a_render_cycle),
    T("toggle_names",     "Show Entity Names",        "",              a_redraw, &prefs.show_names),
    T("toggle_links",     "Show Entity Links",        "",              a_redraw, &prefs.show_links),
    T("toggle_console",   "Show Console",             "",              a_console, &prefs.show_console),
    A("texture_browser",  "Texture Browser...",       "T",             a_texture_browser),

    A("tool_select",      "Selection Tool",           "Shift+S",       a_tool_select),
    A("tool_camera",      "Camera Tool",              "Shift+C",       a_tool_camera),
    A("tool_entity",      "Entity Tool",              "Shift+E",       a_tool_entity),
    A("tool_block",       "Block Tool",               "Shift+B",       a_tool_block),
    A("tool_texture",     "Texture Application Tool", "Shift+A",       a_tool_texture),
    A("tool_clip",        "Clipping Tool",            "Shift+X",       a_tool_clip),
    A("tool_vertex",      "Vertex Tool",              "Shift+V",       a_tool_vertex),
    A("clip_apply",       "Apply Clip",               "Return",        a_clip_apply),
    A("clip_cycle",       "Cycle Clip Side",          "Ctrl+Return",   a_clip_cycle),
    A("apply_texture",    "Apply Current Texture",    "Shift+T",       a_apply_texture),
    T("toggle_texlock",   "Texture Lock",             "Shift+L",       a_redraw, &ed.texlock),
    T("toggle_groups",    "Ignore Groups",            "Ctrl+W",        a_redraw, &ed.ignore_groups),

    A("tie",              "Tie to Entity",            "Ctrl+T",        a_tie),
    A("to_world",         "Move to World",            "Ctrl+Shift+W",  a_to_world),
    A("carve",            "Carve",                    "Ctrl+Shift+C",  a_carve),
    A("hollow",           "Make Hollow...",           "Ctrl+H",        a_hollow),
    A("merge",            "Convex Merge",             "Ctrl+J",        a_merge),
    A("flip_h",           "Flip Horizontally",        "Ctrl+L",        a_flip_h),
    A("flip_v",           "Flip Vertically",          "Ctrl+Shift+L",  a_flip_v),
    A("rotate_cw",        "Rotate 90 Clockwise",      "Ctrl+Shift+R",  a_rotate_cw),
    A("rotate_ccw",       "Rotate 90 Counterclockwise", "",            a_rotate_ccw),
    A("snap_selected",    "Snap Selection to Grid",   "Ctrl+B",        a_snap_selected),
    A("goto_brush",       "Go to Brush Number...",    "Ctrl+Shift+G",  a_goto_brush),
    A("map_info",         "Map Information...",       "",              a_map_info),
    A("check_map",        "Check for Problems",       "Alt+P",         a_check_map),
    A("set_wads",         "Texture WADs...",          "",              a_set_wads),
    A("entity_defs",      "Entity Definitions...",    "",              a_entity_defs),
    A("convert_valve",    "Convert to Valve 220",     "",              a_convert_valve),
    A("reload",           "Reload Textures and Entities", "F5",        a_reload),

    A("nudge_left",       "Nudge Left",               "Left",          a_nudge_left),
    A("nudge_right",      "Nudge Right",              "Right",         a_nudge_right),
    A("nudge_up",         "Nudge Up",                 "Up",            a_nudge_up),
    A("nudge_down",       "Nudge Down",               "Down",          a_nudge_down),
    A("nudge_in",         "Nudge Toward Viewer",      "Prior",         a_nudge_in),
    A("nudge_out",        "Nudge Away from Viewer",   "Next",          a_nudge_out),

    F("cam_forward",      "Camera Forward",           "W",             FLY_FORWARD),
    F("cam_back",         "Camera Back",              "S",             FLY_BACK),
    F("cam_left",         "Camera Strafe Left",       "A",             FLY_LEFT),
    F("cam_right",        "Camera Strafe Right",      "D",             FLY_RIGHT),
    F("cam_up",           "Camera Up",                "E",             FLY_UP),
    F("cam_down",         "Camera Down",              "Q",             FLY_DOWN),

    A("keys_help",        "Keyboard Shortcuts...",    "F1",            a_keys_help),
    A("about",            "About Qotif...",           "",              a_about),
};
int num_actions = (int)ARRAY_COUNT(actions);

/* ------------------------------------------------------------------ */

action_t *action_find(const char *name)
{
    int i;
    for (i = 0; i < num_actions; i++)
        if (!strcmp(actions[i].name, name))
            return &actions[i];
    return NULL;
}

void action_run(action_t *a)
{
    if (!a || a->fly)
        return;
    if (busy() && strcmp(a->name, "cancel")) {
        if (a->toggle)
            ui_sync_toggles();  /* undo the menu item's own state change */
        return;
    }
    if (a->toggle)
        *a->toggle = !*a->toggle;
    if (a->fn)
        a->fn();
    if (a->toggle)
        ui_sync_toggles();
}

int action_run_name(const char *name)
{
    action_t *a = action_find(name);
    if (!a)
        return 0;
    action_run(a);
    return 1;
}

action_t *action_for_key(int mods, unsigned long sym)
{
    int i, k;
    for (i = 0; i < num_actions; i++) {
        if (actions[i].fly)
            continue;
        for (k = 0; k < actions[i].nkeys; k++)
            if (actions[i].keys[k].sym == sym && actions[i].keys[k].mods == mods)
                return &actions[i];
    }
    return NULL;
}

action_t *fly_action_for_key(unsigned long sym)
{
    int i, k;
    for (i = 0; i < num_actions; i++) {
        if (!actions[i].fly)
            continue;
        for (k = 0; k < actions[i].nkeys; k++)
            if (actions[i].keys[k].sym == sym)
                return &actions[i];
    }
    return NULL;
}

static int parse_one(char *s, keybind_t *kb)
{
    char *parts[8];
    int n, i;

    memset(kb, 0, sizeof(*kb));
    n = str_split(s, '+', parts, 8);
    if (n < 1)
        return 0;
    for (i = 0; i < n - 1; i++) {
        if (str_ieq(parts[i], "ctrl") || str_ieq(parts[i], "control"))
            kb->mods |= MOD_CTRL;
        else if (str_ieq(parts[i], "shift"))
            kb->mods |= MOD_SHIFT;
        else if (str_ieq(parts[i], "alt") || str_ieq(parts[i], "meta"))
            kb->mods |= MOD_ALT;
        else
            return 0;
    }
    str_copy(kb->key, parts[n - 1], sizeof(kb->key));
    kb->sym = ui_keysym(kb->key);
    return kb->sym != 0;
}

int keybind_parse(const char *s, keybind_t *out, int max)
{
    char buf[256];
    char *items[MAX_BINDINGS + 2];
    int n, i, count = 0;

    str_copy(buf, s, sizeof(buf));
    n = str_split(buf, ',', items, MAX_BINDINGS + 2);
    for (i = 0; i < n && count < max; i++)
        if (parse_one(items[i], &out[count]))
            count++;
    return count;
}

void keybind_format(const action_t *a, char *buf, int size)
{
    int i;
    buf[0] = 0;
    for (i = 0; i < a->nkeys; i++) {
        char one[64];
        const keybind_t *k = &a->keys[i];
        snprintf(one, sizeof(one), "%s%s%s%s%s",
                 i ? ", " : "",
                 (k->mods & MOD_CTRL) ? "Ctrl+" : "",
                 (k->mods & MOD_SHIFT) ? "Shift+" : "",
                 (k->mods & MOD_ALT) ? "Alt+" : "",
                 k->key);
        if ((int)(strlen(buf) + strlen(one)) < size)
            strcat(buf, one);
    }
}

void actions_reset_binding(action_t *a)
{
    a->nkeys = keybind_parse(a->defkeys, a->keys, MAX_BINDINGS);
}

void actions_load_bindings(void)
{
    int i;
    for (i = 0; i < num_actions; i++) {
        const char *v = ini_get(&prefs.ini, "keys", actions[i].name, actions[i].defkeys);
        actions[i].nkeys = keybind_parse(v, actions[i].keys, MAX_BINDINGS);
    }
}

void actions_save_bindings(void)
{
    int i;
    for (i = 0; i < num_actions; i++) {
        char buf[128];
        keybind_format(&actions[i], buf, sizeof(buf));
        ini_set(&prefs.ini, "keys", actions[i].name, buf);
    }
}
