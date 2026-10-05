/*
 * textures.h - texture collections (Quake WAD2 and Half-Life WAD3)
 */
#ifndef QOTIF_TEXTURES_H
#define QOTIF_TEXTURES_H

#include <stddef.h>

typedef struct texture_s {
    char name[32];
    char source[64];      /* wad file it came from */
    int width, height;
    unsigned char *rgba;
    unsigned int glid;    /* owned by the renderer, 0 = not uploaded */
    float avg[3];         /* average color, used by the flat 3D mode */
    int used;             /* scratch flag for the texture browser */
    int hash_next;
} texture_t;

/* called before a texture is destroyed so the renderer can drop its GL id */
void tex_set_free_hook(void (*fn)(texture_t *t));

void tex_set_palette(const unsigned char *pal768);
void tex_default_palette(void);
int tex_have_palette(void);

int tex_load_wad(const char *path);
void tex_clear(void);
texture_t *tex_find(const char *name);
int tex_count(void);
texture_t *tex_get(int index);

#endif
