/*
 * game.c - game configurations, user preferences and data paths
 */
#include "game.h"

#include <ctype.h>
#include <dirent.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#define MAX_DATA_DIRS 6

static char exedir[PATH_LEN];
static char cfgdir[PATH_LEN];
static char datadirs[MAX_DATA_DIRS][PATH_LEN];
static int ndatadirs;
static int portable;

game_t **games;
int ngames;
prefs_t prefs;

pref_color_t pref_colors[] = {
    { "background2d",  "2D background",       prefs.col_bg2d },
    { "background3d",  "3D background",       prefs.col_bg3d },
    { "grid",          "Grid",                prefs.col_grid },
    { "grid_major",    "Grid (major lines)",  prefs.col_grid_major },
    { "grid_axis",     "Grid (axes)",         prefs.col_grid_axis },
    { "brush",         "Brushes",             prefs.col_brush },
    { "selection",     "Selection",           prefs.col_sel },
    { "clip",          "Clip tool",           prefs.col_clip },
    { "text",          "Text",                prefs.col_text },
    { "camera",        "Camera",              prefs.col_camera },
    { "pointfile",     "Point file (leak)",   prefs.col_pointfile },
    { "links",         "Entity links",        prefs.col_links },
};
int num_pref_colors = (int)ARRAY_COUNT(pref_colors);

/* ------------------------------------------------------------------ */
/* paths                                                               */

static void add_data_dir(const char *dir)
{
    int i;
    if (!dir || !*dir || ndatadirs >= MAX_DATA_DIRS || !dir_exists(dir))
        return;
    for (i = 0; i < ndatadirs; i++)
        if (!strcmp(datadirs[i], dir))
            return;
    str_copy(datadirs[ndatadirs++], dir, PATH_LEN);
}

/* directory that contains the running executable */
static void find_exe_dir(const char *argv0)
{
    char path[PATH_LEN];

    path[0] = 0;
#ifndef _WIN32
    {
        ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (n > 0)
            path[n] = 0;
        else
            path[0] = 0;
    }
    if (!path[0] && argv0 && strchr(argv0, '/')) {
        char *r = realpath(argv0, NULL);
        if (r) {
            str_copy(path, r, sizeof(path));
            free(r);
        }
    }
#else
    (void)argv0;
#endif
    if (path[0])
        path_dirname(exedir, sizeof(exedir), path);
    else
        str_copy(exedir, ".", sizeof(exedir));
}

static int dir_writable(const char *dir)
{
#ifndef _WIN32
    return dir_exists(dir) && access(dir, W_OK) == 0;
#else
    return dir_exists(dir);
#endif
}

/*
 * Qotif is portable: data/ and config/ live next to the executable, so the
 * whole directory can be copied anywhere (or to a USB stick) and run without
 * installing.  Only when that directory is read-only does the configuration
 * go to $XDG_CONFIG_HOME/qotif instead.
 */
void paths_init(const char *argv0)
{
    char tmp[PATH_LEN];

    find_exe_dir(argv0);
    path_join(cfgdir, sizeof(cfgdir), exedir, "config");
    if (dir_writable(cfgdir) || (dir_writable(exedir) && dir_make_path(cfgdir))) {
        portable = 1;
    } else {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        const char *home = getenv("HOME");
        if (xdg && *xdg)
            path_join(cfgdir, sizeof(cfgdir), xdg, "qotif");
        else if (home && *home) {
            path_join(tmp, sizeof(tmp), home, ".config");
            path_join(cfgdir, sizeof(cfgdir), tmp, "qotif");
        } else {
            str_copy(cfgdir, ".qotif", sizeof(cfgdir));
        }
        dir_make_path(cfgdir);
    }
    path_join(tmp, sizeof(tmp), cfgdir, "games");
    dir_make_path(tmp);

    /* user overrides first, then the bundled data */
    ndatadirs = 0;
    add_data_dir(cfgdir);
    add_data_dir(getenv("QOTIF_DATA"));
    path_join(tmp, sizeof(tmp), exedir, "data");
    add_data_dir(tmp);
    add_data_dir("./data");
}

const char *exe_dir(void)
{
    return exedir;
}

int paths_portable(void)
{
    return portable;
}

const char *config_dir(void)
{
    return cfgdir;
}

int data_dir_count(void)
{
    return ndatadirs;
}

const char *data_dir(int i)
{
    return (i >= 0 && i < ndatadirs) ? datadirs[i] : "";
}

int find_data_file(const char *rel, char *out, size_t size)
{
    int i;
    if (path_is_absolute(rel)) {
        str_copy(out, rel, size);
        return file_exists(rel);
    }
    for (i = 0; i < ndatadirs; i++) {
        path_join(out, size, datadirs[i], rel);
        if (file_exists(out))
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* games                                                               */

static void game_free(game_t *g)
{
    int i, j;
    for (i = 0; i < g->nprofiles; i++) {
        for (j = 0; j < g->profiles[i].ncmds; j++)
            free(g->profiles[i].cmds[j]);
        free(g->profiles[i].cmds);
    }
    free(g->profiles);
    ini_free(&g->ini);
    free(g);
}

static game_t *game_load(const char *path)
{
    game_t *g = xcalloc(1, sizeof(game_t));
    char base[PATH_LEN];
    int i, j;

    ini_init(&g->ini);
    if (!ini_load(&g->ini, path)) {
        game_free(g);
        return NULL;
    }
    path_strip_ext(base, sizeof(base), path_basename(path));
    str_copy(g->cfgpath, path, sizeof(g->cfgpath));
    path_dirname(g->cfgdir, sizeof(g->cfgdir), path);
    str_copy(g->name, ini_get(&g->ini, "game", "name", base), sizeof(g->name));
    str_copy(g->basedir, ini_get(&g->ini, "game", "basedir", "id1"), sizeof(g->basedir));
    str_copy(g->palette, ini_get(&g->ini, "game", "palette", ""), sizeof(g->palette));
    str_copy(g->entities, ini_get(&g->ini, "game", "entities", ""), sizeof(g->entities));
    str_copy(g->defaulttex, ini_get(&g->ini, "game", "defaulttexture", ""), sizeof(g->defaulttex));
    str_copy(g->wads, ini_get(&g->ini, "game", "wads", ""), sizeof(g->wads));
    g->valve220 = str_ieq(ini_get(&g->ini, "game", "mapformat", "standard"), "valve220")
               || str_ieq(ini_get(&g->ini, "game", "mapformat", ""), "valve");

    /* [profile <name>] sections with repeated "cmd" keys */
    for (i = 0; i < g->ini.n; i++) {
        const char *sec = g->ini.e[i].section;
        profile_t *p = NULL;
        if (!str_iprefix(sec, "profile "))
            continue;
        for (j = 0; j < g->nprofiles; j++)
            if (str_ieq(g->profiles[j].name, sec + 8))
                p = &g->profiles[j];
        if (!p) {
            g->profiles = xrealloc(g->profiles, sizeof(profile_t) * (size_t)(g->nprofiles + 1));
            p = &g->profiles[g->nprofiles++];
            memset(p, 0, sizeof(*p));
            str_copy(p->name, sec + 8, sizeof(p->name));
        }
        if (str_ieq(g->ini.e[i].key, "cmd")) {
            p->cmds = xrealloc(p->cmds, sizeof(char *) * (size_t)(p->ncmds + 1));
            p->cmds[p->ncmds++] = xstrdup(g->ini.e[i].value);
        }
    }
    return g;
}

void games_scan(void)
{
    int i, j;

    for (i = 0; i < ngames; i++)
        game_free(games[i]);
    free(games);
    games = NULL;
    ngames = 0;

    for (i = 0; i < ndatadirs; i++) {
        char dirpath[PATH_LEN];
        DIR *dir;
        struct dirent *de;

        path_join(dirpath, sizeof(dirpath), datadirs[i], "games");
        dir = opendir(dirpath);
        if (!dir)
            continue;
        while ((de = readdir(dir)) != NULL) {
            char path[PATH_LEN];
            game_t *g;
            int dup = 0;

            if (!str_ieq(path_ext(de->d_name), ".cfg"))
                continue;
            path_join(path, sizeof(path), dirpath, de->d_name);
            g = game_load(path);
            if (!g)
                continue;
            for (j = 0; j < ngames; j++)
                if (str_ieq(games[j]->name, g->name))
                    dup = 1;
            if (dup) {
                game_free(g);
                continue;
            }
            games = xrealloc(games, sizeof(game_t *) * (size_t)(ngames + 1));
            games[ngames++] = g;
            log_info("game configuration '%s' (%s)", g->name, path);
        }
        closedir(dir);
    }
    if (!ngames) {
        /* built-in fallback so the editor always has a game */
        game_t *g = xcalloc(1, sizeof(game_t));
        ini_init(&g->ini);
        str_copy(g->name, "Quake", sizeof(g->name));
        str_copy(g->basedir, "id1", sizeof(g->basedir));
        str_copy(g->palette, "gfx/palette.lmp", sizeof(g->palette));
        games = xmalloc(sizeof(game_t *));
        games[0] = g;
        ngames = 1;
        log_warn("no game configurations found; using built-in Quake defaults");
    }
}

game_t *game_find(const char *name)
{
    int i;
    for (i = 0; i < ngames; i++)
        if (str_ieq(games[i]->name, name))
            return games[i];
    return NULL;
}

game_t *game_current(void)
{
    game_t *g = game_find(prefs.game);
    return g ? g : (ngames ? games[0] : NULL);
}

static void lookup_var(const game_t *g, const char *mapfile, const char *name, char *out, size_t size)
{
    char tmp[PATH_LEN];
    const char *gamepath = prefs_game_get(g->name, "path", "");
    const char *v;

    out[0] = 0;
    if (!strcmp(name, "MAP_FILE")) {
        str_copy(out, mapfile, size);
    } else if (!strcmp(name, "MAP_DIR")) {
        path_dirname(out, size, mapfile);
    } else if (!strcmp(name, "MAP_BASE")) {
        path_strip_ext(out, size, mapfile);
    } else if (!strcmp(name, "MAP_NAME")) {
        path_strip_ext(tmp, sizeof(tmp), path_basename(mapfile));
        str_copy(out, tmp, size);
    } else if (!strcmp(name, "GAME_DIR")) {
        str_copy(out, gamepath, size);
    } else if (!strcmp(name, "BASE_DIR")) {
        path_join(out, size, gamepath, g->basedir);
    } else if (!strcmp(name, "BASE_NAME")) {
        str_copy(out, g->basedir, size);
    } else if (!strcmp(name, "MOD")) {
        v = prefs_game_get(g->name, "mod", "");
        if (!*v)
            v = ini_get(&g->ini, "variables", "MOD", g->basedir);
        str_copy(out, v, size);
    } else if (!strcmp(name, "CONFIG_DIR")) {
        str_copy(out, cfgdir, size);
    } else if ((v = prefs_game_get(g->name, name, NULL)) != NULL && *v) {
        str_copy(out, v, size);
    } else if ((v = ini_get(&g->ini, "variables", name, NULL)) != NULL) {
        str_copy(out, v, size);
    } else if ((v = getenv(name)) != NULL) {
        str_copy(out, v, size);
    } else {
        log_warn("compile profile: undefined variable ${%s}", name);
    }
}

void game_expand(const game_t *g, const char *mapfile, const char *in, char *out, size_t size)
{
    strbuf_t sb;

    sb_init(&sb);
    while (*in) {
        if (in[0] == '$' && in[1] == '{') {
            const char *end = strchr(in + 2, '}');
            if (end) {
                char name[128], value[PATH_LEN];
                size_t n = (size_t)(end - in - 2);
                if (n >= sizeof(name))
                    n = sizeof(name) - 1;
                memcpy(name, in + 2, n);
                name[n] = 0;
                lookup_var(g, mapfile, name, value, sizeof(value));
                sb_append(&sb, value);
                in = end + 1;
                continue;
            }
        }
        sb_appendn(&sb, in, 1);
        in++;
    }
    str_copy(out, sb.data ? sb.data : "", size);
    sb_free(&sb);
}

/* ------------------------------------------------------------------ */
/* preferences                                                         */

static void set_color(float *c, float r, float g, float b)
{
    c[0] = r;
    c[1] = g;
    c[2] = b;
}

void prefs_defaults(void)
{
    str_copy(prefs.game, "Quake", sizeof(prefs.game));
    prefs.grid = 16;
    prefs.grid_major = 64;
    prefs.show_grid = 1;
    prefs.snap = 1;
    prefs.texlock = 1;
    prefs.undo_levels = 100;
    prefs.fov = 75.0;
    prefs.sensitivity = 0.25;
    prefs.flyspeed = 512.0;
    prefs.invert_mouse = 0;
    prefs.layout = LAYOUT_FOUR;
    prefs.view_types[0] = 0;    /* 3D */
    prefs.view_types[1] = 1;    /* top */
    prefs.view_types[2] = 2;    /* front */
    prefs.view_types[3] = 3;    /* side */
    prefs.split_x = 500;
    prefs.split_y = 500;
    prefs.show_names = 1;
    prefs.show_links = 1;
    prefs.show_console = 1;
    prefs.tex_linear = 0;
    prefs.tex_mipmaps = 1;
    prefs.thumb_size = 128;
    str_copy(prefs.font, "-misc-fixed-medium-r-normal--13-*-*-*-*-*-iso8859-1", sizeof(prefs.font));
    /* Hammer-like palette: black 2D views, gray grid, white brushes, red selection */
    set_color(prefs.col_bg2d, 0.0f, 0.0f, 0.0f);
    set_color(prefs.col_bg3d, 0.0f, 0.0f, 0.0f);
    set_color(prefs.col_grid, 0.16f, 0.16f, 0.16f);
    set_color(prefs.col_grid_major, 0.32f, 0.32f, 0.32f);
    set_color(prefs.col_grid_axis, 0.0f, 0.39f, 0.39f);
    set_color(prefs.col_brush, 1.0f, 1.0f, 1.0f);
    set_color(prefs.col_sel, 1.0f, 0.0f, 0.0f);
    set_color(prefs.col_clip, 1.0f, 1.0f, 0.0f);
    set_color(prefs.col_text, 1.0f, 1.0f, 1.0f);
    set_color(prefs.col_camera, 0.0f, 1.0f, 1.0f);
    set_color(prefs.col_pointfile, 1.0f, 0.2f, 0.2f);
    set_color(prefs.col_links, 1.0f, 0.75f, 0.0f);
}

static void prefs_path(char *out, size_t size)
{
    path_join(out, size, cfgdir, "prefs.cfg");
}

void prefs_load(void)
{
    char path[PATH_LEN];
    int i;
    ini_t *ini = &prefs.ini;

    prefs_defaults();
    ini_free(ini);
    prefs_path(path, sizeof(path));
    if (!ini_load(ini, path))
        log_info("no preferences yet (%s); using defaults", path);

    str_copy(prefs.game, ini_get(ini, "general", "game", prefs.game), sizeof(prefs.game));
    prefs.grid = ini_get_int(ini, "general", "grid", prefs.grid);
    prefs.grid_major = ini_get_int(ini, "general", "grid_major", prefs.grid_major);
    prefs.show_grid = ini_get_bool(ini, "general", "show_grid", prefs.show_grid);
    prefs.snap = ini_get_bool(ini, "general", "snap", prefs.snap);
    prefs.texlock = ini_get_bool(ini, "general", "texture_lock", prefs.texlock);
    prefs.undo_levels = ini_get_int(ini, "general", "undo_levels", prefs.undo_levels);
    prefs.fov = ini_get_double(ini, "view", "fov", prefs.fov);
    prefs.sensitivity = ini_get_double(ini, "view", "mouse_sensitivity", prefs.sensitivity);
    prefs.flyspeed = ini_get_double(ini, "view", "fly_speed", prefs.flyspeed);
    prefs.invert_mouse = ini_get_bool(ini, "view", "invert_mouse", prefs.invert_mouse);
    prefs.layout = ini_get_int(ini, "view", "layout", prefs.layout);
    prefs.split_x = ini_get_int(ini, "view", "split_x", prefs.split_x);
    prefs.split_y = ini_get_int(ini, "view", "split_y", prefs.split_y);
    if (prefs.split_x < 100 || prefs.split_x > 900)
        prefs.split_x = 500;
    if (prefs.split_y < 100 || prefs.split_y > 900)
        prefs.split_y = 500;
    for (i = 0; i < 4; i++) {
        char key[32];
        snprintf(key, sizeof(key), "view%d", i);
        prefs.view_types[i] = ini_get_int(ini, "view", key, prefs.view_types[i]);
        if (prefs.view_types[i] < 0 || prefs.view_types[i] > 3)
            prefs.view_types[i] = 0;
    }
    prefs.show_names = ini_get_bool(ini, "view", "show_names", prefs.show_names);
    prefs.show_links = ini_get_bool(ini, "view", "show_links", prefs.show_links);
    prefs.show_console = ini_get_bool(ini, "view", "show_console", prefs.show_console);
    prefs.tex_linear = ini_get_bool(ini, "view", "texture_linear", prefs.tex_linear);
    prefs.tex_mipmaps = ini_get_bool(ini, "view", "texture_mipmaps", prefs.tex_mipmaps);
    prefs.thumb_size = ini_get_int(ini, "view", "thumbnail_size", prefs.thumb_size);
    str_copy(prefs.font, ini_get(ini, "view", "font", prefs.font), sizeof(prefs.font));
    for (i = 0; i < num_pref_colors; i++) {
        const char *v = ini_get(ini, "colors", pref_colors[i].key, NULL);
        float r, g, b;
        if (v && sscanf(v, "%f %f %f", &r, &g, &b) == 3)
            set_color(pref_colors[i].rgb, r, g, b);
    }
    if (prefs.grid < 1)
        prefs.grid = 1;
    if (prefs.grid_major < 2)
        prefs.grid_major = 64;
    if (prefs.undo_levels < 1)
        prefs.undo_levels = 1;
    if (prefs.layout < LAYOUT_FOUR || prefs.layout > LAYOUT_ONE)
        prefs.layout = LAYOUT_FOUR;
}

void prefs_save(void)
{
    char path[PATH_LEN];
    int i;
    ini_t *ini = &prefs.ini;

    ini_set(ini, "general", "game", prefs.game);
    ini_set_int(ini, "general", "grid", prefs.grid);
    ini_set_int(ini, "general", "grid_major", prefs.grid_major);
    ini_set_int(ini, "general", "show_grid", prefs.show_grid);
    ini_set_int(ini, "general", "snap", prefs.snap);
    ini_set_int(ini, "general", "texture_lock", prefs.texlock);
    ini_set_int(ini, "general", "undo_levels", prefs.undo_levels);
    ini_set_double(ini, "view", "fov", prefs.fov);
    ini_set_double(ini, "view", "mouse_sensitivity", prefs.sensitivity);
    ini_set_double(ini, "view", "fly_speed", prefs.flyspeed);
    ini_set_int(ini, "view", "invert_mouse", prefs.invert_mouse);
    ini_set_int(ini, "view", "layout", prefs.layout);
    ini_set_int(ini, "view", "split_x", prefs.split_x);
    ini_set_int(ini, "view", "split_y", prefs.split_y);
    for (i = 0; i < 4; i++) {
        char key[32];
        snprintf(key, sizeof(key), "view%d", i);
        ini_set_int(ini, "view", key, prefs.view_types[i]);
    }
    ini_set_int(ini, "view", "show_names", prefs.show_names);
    ini_set_int(ini, "view", "show_links", prefs.show_links);
    ini_set_int(ini, "view", "show_console", prefs.show_console);
    ini_set_int(ini, "view", "texture_linear", prefs.tex_linear);
    ini_set_int(ini, "view", "texture_mipmaps", prefs.tex_mipmaps);
    ini_set_int(ini, "view", "thumbnail_size", prefs.thumb_size);
    ini_set(ini, "view", "font", prefs.font);
    for (i = 0; i < num_pref_colors; i++) {
        char buf[64];
        const float *c = pref_colors[i].rgb;
        snprintf(buf, sizeof(buf), "%.3f %.3f %.3f", c[0], c[1], c[2]);
        fmt_dot_decimal(buf);
        ini_set(ini, "colors", pref_colors[i].key, buf);
    }
    prefs_path(path, sizeof(path));
    if (!ini_save(ini, path))
        log_error("cannot write preferences to %s", path);
}

const char *prefs_game_get(const char *game, const char *key, const char *def)
{
    char section[128];
    snprintf(section, sizeof(section), "game %s", game);
    return ini_get(&prefs.ini, section, key, def);
}

void prefs_game_set(const char *game, const char *key, const char *value)
{
    char section[128];
    snprintf(section, sizeof(section), "game %s", game);
    ini_set(&prefs.ini, section, key, value);
}
