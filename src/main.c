/*
 * main.c - Qotif, a Quake map editor for X11/Motif and OpenGL 1.x
 *
 * usage: qotif [file.map]
 */
#include "editor.h"
#include "game.h"
#include "ui.h"

int main(int argc, char **argv)
{
    paths_init(argc > 0 ? argv[0] : NULL);
    prefs_load();
    games_scan();
    if (!game_find(prefs.game) && ngames)
        str_copy(prefs.game, games[0]->name, sizeof(prefs.game));
    ed_init();
    return ui_main(argc, argv);
}
