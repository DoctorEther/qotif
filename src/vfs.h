/*
 * vfs.h - game file system: loose files and Quake .pak archives
 */
#ifndef QOTIF_VFS_H
#define QOTIF_VFS_H

#include <stddef.h>

void vfs_shutdown(void);
/* adds a game directory (and its pak0..pak9.pak); later additions take
 * priority, so add the base game first and the mod last */
void vfs_add_gamedir(const char *dir);
/* loads a file by its game-relative name; caller frees */
unsigned char *vfs_load(const char *name, size_t *len);

#endif
