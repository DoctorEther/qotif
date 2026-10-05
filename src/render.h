/*
 * render.h - OpenGL 1.x fixed-function rendering of views and textures
 */
#ifndef QOTIF_RENDER_H
#define QOTIF_RENDER_H

#include "textures.h"
#include "view.h"

void render_init(void);
void render_set_font(unsigned int list_base, int char_w, int char_h);
void render_view(view_t *v);
void render_texture_free(texture_t *t);
void render_reset_textures(void);
unsigned int render_texture_id(texture_t *t);

typedef struct texbrowser_s {
    int width, height;
    int scroll;
    int thumb;
    char filter[64];
    int used_only;
    int content_height;
} texbrowser_t;

void texbrowser_update_used(void);
void texbrowser_render(texbrowser_t *tb);
int texbrowser_hit(texbrowser_t *tb, int x, int y);
int texbrowser_find(texbrowser_t *tb, const char *name);
void render_texture_preview(int w, int h);

#endif
