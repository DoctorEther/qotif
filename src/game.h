/*
 * game.h - game configurations, user preferences and data paths
 *
 * Game configurations live in games/<name>.cfg inside any data directory;
 * a file in the user's config directory overrides a system one with the
 * same game name.  Preferences are stored in config/prefs.cfg next to the
 * executable (portable mode), or in ~/.config/qotif when that is read-only.
 */
#ifndef QOTIF_GAME_H
#define QOTIF_GAME_H

#include "common.h"
#include "ini.h"

typedef struct profile_s {
    char name[64];
    char **cmds;
    int ncmds;
} profile_t;

typedef struct game_s {
    char name[64];
    char cfgpath[PATH_LEN];
    char cfgdir[PATH_LEN];
    char basedir[64];        /* "id1", "valve", ... */
    char palette[256];       /* game-relative palette file, may be empty */
    char entities[PATH_LEN]; /* .fgd or .def, relative to the cfg file */
    char defaulttex[64];
    char wads[1024];         /* ';' separated, relative to the game path */
    int valve220;            /* map format for new maps */
    profile_t *profiles;
    int nprofiles;
    ini_t ini;
} game_t;

extern game_t **games;
extern int ngames;

void paths_init(const char *argv0);
const char *exe_dir(void);
int paths_portable(void);
const char *config_dir(void);
int data_dir_count(void);
const char *data_dir(int i);
/* finds a file relative to the data directories */
int find_data_file(const char *rel, char *out, size_t size);

void games_scan(void);
game_t *game_find(const char *name);
game_t *game_current(void);

/* "${NAME}" expansion used by compile profiles */
void game_expand(const game_t *g, const char *mapfile, const char *in, char *out, size_t size);

enum { LAYOUT_FOUR, LAYOUT_TWO, LAYOUT_ONE };

typedef struct prefs_s {
    char game[64];
    int grid;
    int grid_major;
    int show_grid;
    int snap;
    int texlock;
    int undo_levels;
    double fov;
    double sensitivity;
    double flyspeed;
    int invert_mouse;
    int layout;
    int view_types[4];
    int split_x, split_y;    /* view splitter positions, 0..1000 */
    int show_names;
    int show_links;
    int show_console;
    int tex_linear;
    int thumb_size;
    char font[256];
    float col_bg2d[3], col_bg3d[3];
    float col_grid[3], col_grid_major[3], col_grid_axis[3];
    float col_brush[3], col_sel[3], col_clip[3], col_text[3];
    float col_camera[3], col_pointfile[3], col_links[3];
    ini_t ini;
} prefs_t;

extern prefs_t prefs;

typedef struct pref_color_s {
    const char *key;
    const char *label;
    float *rgb;
} pref_color_t;

extern pref_color_t pref_colors[];
extern int num_pref_colors;

void prefs_defaults(void);
void prefs_load(void);
void prefs_save(void);
/* per game settings, stored in the [game <name>] section */
const char *prefs_game_get(const char *game, const char *key, const char *def);
void prefs_game_set(const char *game, const char *key, const char *value);

#endif
