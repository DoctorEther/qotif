/*
 * textures.c - texture collections (Quake WAD2 and Half-Life WAD3)
 */
#include "textures.h"
#include "common.h"

#include <ctype.h>

#define HASH_SIZE 4096

static unsigned char palette[768];
static int palette_loaded;
static texture_t **texs;
static int ntexs, maxtexs;
static int hash_heads[HASH_SIZE];   /* index + 1, 0 = empty */
static void (*free_hook)(texture_t *t);

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static unsigned hash_name(const char *s)
{
    unsigned h = 5381;
    for (; *s; s++)
        h = h * 33 + (unsigned)tolower((unsigned char)*s);
    return h % HASH_SIZE;
}

void tex_set_free_hook(void (*fn)(texture_t *t))
{
    free_hook = fn;
}

void tex_set_palette(const unsigned char *pal768)
{
    memcpy(palette, pal768, 768);
    palette_loaded = 1;
}

void tex_default_palette(void)
{
    int i;
    for (i = 0; i < 256; i++)
        palette[i * 3] = palette[i * 3 + 1] = palette[i * 3 + 2] = (unsigned char)i;
    palette_loaded = 0;
}

int tex_have_palette(void)
{
    return palette_loaded;
}

void tex_clear(void)
{
    int i;
    for (i = 0; i < ntexs; i++) {
        if (free_hook)
            free_hook(texs[i]);
        free(texs[i]->rgba);
        free(texs[i]);
    }
    free(texs);
    texs = NULL;
    ntexs = maxtexs = 0;
    memset(hash_heads, 0, sizeof(hash_heads));
}

texture_t *tex_find(const char *name)
{
    int i;
    if (!name || !*name || !ntexs)
        return NULL;
    for (i = hash_heads[hash_name(name)]; i; i = texs[i - 1]->hash_next)
        if (str_ieq(texs[i - 1]->name, name))
            return texs[i - 1];
    return NULL;
}

int tex_count(void)
{
    return ntexs;
}

texture_t *tex_get(int index)
{
    return (index >= 0 && index < ntexs) ? texs[index] : NULL;
}

static void add_miptex(const unsigned char *data, size_t size, int wad3, const char *source)
{
    char name[17];
    unsigned w, h, ofs0, i;
    const unsigned char *pal = palette;
    texture_t *t;
    unsigned hidx;
    double sum[3] = { 0, 0, 0 };
    int transparent;

    if (size < 40)
        return;
    memcpy(name, data, 16);
    name[16] = 0;
    w = rd32(data + 16);
    h = rd32(data + 20);
    ofs0 = rd32(data + 24);
    if (!name[0] || !w || !h || w > 4096 || h > 4096 || !ofs0)
        return;
    if ((size_t)ofs0 + (size_t)w * h > size)
        return;
    if (wad3) {
        size_t palofs = (size_t)rd32(data + 36) + (size_t)(w / 8) * (h / 8);
        if (palofs + 2 + 768 <= size)
            pal = data + palofs + 2;
    }
    if (tex_find(name))
        return;

    t = xcalloc(1, sizeof(texture_t));
    str_copy(t->name, name, sizeof(t->name));
    str_copy(t->source, source, sizeof(t->source));
    t->width = (int)w;
    t->height = (int)h;
    t->rgba = xmalloc((size_t)w * h * 4);
    transparent = name[0] == '{';
    for (i = 0; i < w * h; i++) {
        unsigned c = data[ofs0 + i];
        t->rgba[i * 4 + 0] = pal[c * 3 + 0];
        t->rgba[i * 4 + 1] = pal[c * 3 + 1];
        t->rgba[i * 4 + 2] = pal[c * 3 + 2];
        t->rgba[i * 4 + 3] = (transparent && c == 255) ? 0 : 255;
        sum[0] += pal[c * 3 + 0];
        sum[1] += pal[c * 3 + 1];
        sum[2] += pal[c * 3 + 2];
    }
    for (i = 0; i < 3; i++)
        t->avg[i] = (float)(sum[i] / (255.0 * w * h));

    if (ntexs >= maxtexs) {
        maxtexs = maxtexs ? maxtexs * 2 : 256;
        texs = xrealloc(texs, sizeof(texture_t *) * (size_t)maxtexs);
    }
    texs[ntexs++] = t;
    hidx = hash_name(t->name);
    t->hash_next = hash_heads[hidx];
    hash_heads[hidx] = ntexs;
}

int tex_load_wad(const char *path)
{
    size_t len;
    unsigned char *data = (unsigned char *)file_read_all(path, &len);
    unsigned numlumps, infoofs, i;
    int wad3, before = ntexs;

    if (!data) {
        log_warn("cannot read wad '%s'", path);
        return 0;
    }
    if (len < 12 || (memcmp(data, "WAD2", 4) != 0 && memcmp(data, "WAD3", 4) != 0)) {
        log_warn("'%s' is not a WAD2/WAD3 file", path);
        free(data);
        return 0;
    }
    wad3 = data[3] == '3';
    numlumps = rd32(data + 4);
    infoofs = rd32(data + 8);
    if ((size_t)infoofs + (size_t)numlumps * 32 > len) {
        log_warn("'%s': corrupt lump directory", path);
        free(data);
        return 0;
    }
    for (i = 0; i < numlumps; i++) {
        const unsigned char *l = data + infoofs + i * 32;
        unsigned filepos = rd32(l), disksize = rd32(l + 4);
        unsigned char type = l[12], compression = l[13];

        if (compression || (type != 0x44 && type != 0x43))
            continue;
        if ((size_t)filepos + disksize > len)
            continue;
        add_miptex(data + filepos, disksize, wad3 || type == 0x43, path_basename(path));
    }
    free(data);
    log_info("loaded %d textures from %s", ntexs - before, path);
    return ntexs - before;
}
