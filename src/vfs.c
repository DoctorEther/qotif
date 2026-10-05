/*
 * vfs.c - game file system: loose files and Quake .pak archives
 */
#include "vfs.h"
#include "common.h"

#define MAX_GAMEDIRS 8
#define MAX_PAKS 10

typedef struct pakfile_s {
    char name[57];
    unsigned pos, len;
} pakfile_t;

typedef struct pak_s {
    char path[PATH_LEN];
    pakfile_t *files;
    int nfiles;
} pak_t;

typedef struct gamedir_s {
    char dir[PATH_LEN];
    pak_t paks[MAX_PAKS];
    int npaks;
} gamedir_t;

static gamedir_t gamedirs[MAX_GAMEDIRS];
static int ngamedirs;

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int pak_open(pak_t *pak, const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char hdr[12], *dir;
    unsigned dirofs, dirlen;
    int i;

    if (!f)
        return 0;
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "PACK", 4) != 0) {
        fclose(f);
        return 0;
    }
    dirofs = rd32(hdr + 4);
    dirlen = rd32(hdr + 8);
    if (dirlen % 64 || dirlen > 64u * 65536u) {
        fclose(f);
        return 0;
    }
    dir = xmalloc(dirlen ? dirlen : 1);
    if (fseek(f, (long)dirofs, SEEK_SET) != 0 || fread(dir, 1, dirlen, f) != dirlen) {
        free(dir);
        fclose(f);
        return 0;
    }
    fclose(f);
    str_copy(pak->path, path, sizeof(pak->path));
    pak->nfiles = (int)(dirlen / 64);
    pak->files = xcalloc((size_t)pak->nfiles, sizeof(pakfile_t));
    for (i = 0; i < pak->nfiles; i++) {
        memcpy(pak->files[i].name, dir + i * 64, 56);
        pak->files[i].name[56] = 0;
        pak->files[i].pos = rd32(dir + i * 64 + 56);
        pak->files[i].len = rd32(dir + i * 64 + 60);
    }
    free(dir);
    log_info("added %s (%d files)", path, pak->nfiles);
    return 1;
}

void vfs_shutdown(void)
{
    int i, j;
    for (i = 0; i < ngamedirs; i++)
        for (j = 0; j < gamedirs[i].npaks; j++)
            free(gamedirs[i].paks[j].files);
    memset(gamedirs, 0, sizeof(gamedirs));
    ngamedirs = 0;
}

void vfs_add_gamedir(const char *dir)
{
    gamedir_t *g;
    int i;

    if (!dir || !*dir || ngamedirs >= MAX_GAMEDIRS || !dir_exists(dir))
        return;
    memmove(&gamedirs[1], &gamedirs[0], sizeof(gamedir_t) * (size_t)ngamedirs);
    ngamedirs++;
    g = &gamedirs[0];
    memset(g, 0, sizeof(*g));
    str_copy(g->dir, dir, sizeof(g->dir));
    /* higher numbered paks override lower ones */
    for (i = MAX_PAKS - 1; i >= 0; i--) {
        char name[32], path[PATH_LEN];
        snprintf(name, sizeof(name), "pak%d.pak", i);
        path_join(path, sizeof(path), dir, name);
        if (file_exists(path) && pak_open(&g->paks[g->npaks], path))
            g->npaks++;
    }
}

static unsigned char *pak_load(const pak_t *pak, const pakfile_t *pf, size_t *len)
{
    FILE *f = fopen(pak->path, "rb");
    unsigned char *data;

    if (!f)
        return NULL;
    data = xmalloc(pf->len + 1);
    if (fseek(f, (long)pf->pos, SEEK_SET) != 0 || fread(data, 1, pf->len, f) != pf->len) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[pf->len] = 0;
    if (len)
        *len = pf->len;
    return data;
}

unsigned char *vfs_load(const char *name, size_t *len)
{
    int i, j, k;

    for (i = 0; i < ngamedirs; i++) {
        char path[PATH_LEN];
        path_join(path, sizeof(path), gamedirs[i].dir, name);
        if (file_exists(path))
            return (unsigned char *)file_read_all(path, len);
        for (j = 0; j < gamedirs[i].npaks; j++) {
            const pak_t *pak = &gamedirs[i].paks[j];
            for (k = 0; k < pak->nfiles; k++)
                if (str_ieq(pak->files[k].name, name))
                    return pak_load(pak, &pak->files[k], len);
        }
    }
    return NULL;
}
