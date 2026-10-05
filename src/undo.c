/*
 * undo.c - undo/redo by whole-map snapshots
 *
 * Each step stores the map serialized as .map text plus the selection
 * flags (in traversal order).  Simple and always consistent, and cheap
 * enough for Quake sized maps.
 */
#include "editor.h"
#include "game.h"
#include "ui.h"

typedef struct snapshot_s {
    char *text;
    unsigned char *sel;
    int nsel;
    char desc[64];
} snapshot_t;

static snapshot_t *undo_stack, *redo_stack;
static int nundo, nredo, maxundo, maxredo;

static void snap_free(snapshot_t *s)
{
    free(s->text);
    free(s->sel);
    s->text = NULL;
    s->sel = NULL;
}

static void snap_take(snapshot_t *s, const char *desc)
{
    entity_t *e;
    brush_t *b;
    int n = 0, i;

    s->text = map_write_string(ed.map, 0);
    for (e = ed.map->entities; e; e = e->next) {
        n++;
        for (b = e->brushes; b; b = b->next)
            n += 1 + b->nfaces;
    }
    s->sel = xmalloc((size_t)n + 1);
    s->nsel = n;
    n = 0;
    for (e = ed.map->entities; e; e = e->next) {
        s->sel[n++] = (unsigned char)e->selected;
        for (b = e->brushes; b; b = b->next) {
            s->sel[n++] = (unsigned char)b->selected;
            for (i = 0; i < b->nfaces; i++)
                s->sel[n++] = (unsigned char)b->faces[i].selected;
        }
    }
    str_copy(s->desc, desc, sizeof(s->desc));
}

static int snap_restore(const snapshot_t *s)
{
    char err[256];
    map_t *m = map_parse(s->text, err, sizeof(err));
    entity_t *e;
    brush_t *b;
    int n = 0, i;

    if (!m) {
        log_error("undo: cannot restore map: %s", err);
        return 0;
    }
    for (e = m->entities; e; e = e->next) {
        if (n < s->nsel)
            e->selected = s->sel[n];
        n++;
        for (b = e->brushes; b; b = b->next) {
            if (n < s->nsel)
                b->selected = s->sel[n];
            n++;
            for (i = 0; i < b->nfaces; i++, n++)
                if (n < s->nsel)
                    b->faces[i].selected = s->sel[n];
        }
    }
    str_copy(m->path, ed.map->path, sizeof(m->path));
    m->dirty = 1;
    eclass_bind_map(m);
    map_free(ed.map);
    ed.map = m;
    return 1;
}

static void push(snapshot_t **stack, int *n, int *max, snapshot_t *s)
{
    if (*n >= *max) {
        *max = *max ? *max * 2 : 32;
        *stack = xrealloc(*stack, sizeof(snapshot_t) * (size_t)*max);
    }
    (*stack)[(*n)++] = *s;
}

void undo_clear(void)
{
    int i;
    for (i = 0; i < nundo; i++)
        snap_free(&undo_stack[i]);
    for (i = 0; i < nredo; i++)
        snap_free(&redo_stack[i]);
    nundo = nredo = 0;
}

void undo_push(const char *desc)
{
    snapshot_t s;
    int i;

    if (tr_active())
        return;
    snap_take(&s, desc);
    push(&undo_stack, &nundo, &maxundo, &s);
    while (nundo > prefs.undo_levels) {
        snap_free(&undo_stack[0]);
        memmove(&undo_stack[0], &undo_stack[1], sizeof(snapshot_t) * (size_t)(nundo - 1));
        nundo--;
    }
    for (i = 0; i < nredo; i++)
        snap_free(&redo_stack[i]);
    nredo = 0;
    ed_mark_dirty();
}

static void after_restore(void)
{
    ui_map_changed();
    sel_changed();
}

int undo_undo(void)
{
    snapshot_t cur, prev;

    if (!nundo)
        return 0;
    prev = undo_stack[nundo - 1];
    snap_take(&cur, prev.desc);
    if (!snap_restore(&prev)) {
        snap_free(&cur);
        return 0;
    }
    nundo--;
    snap_free(&prev);
    push(&redo_stack, &nredo, &maxredo, &cur);
    ui_status("Undo %s", cur.desc);
    after_restore();
    return 1;
}

int undo_redo(void)
{
    snapshot_t cur, next;

    if (!nredo)
        return 0;
    next = redo_stack[nredo - 1];
    snap_take(&cur, next.desc);
    if (!snap_restore(&next)) {
        snap_free(&cur);
        return 0;
    }
    nredo--;
    snap_free(&next);
    push(&undo_stack, &nundo, &maxundo, &cur);
    ui_status("Redo %s", cur.desc);
    after_restore();
    return 1;
}

const char *undo_desc(void)
{
    return nundo ? undo_stack[nundo - 1].desc : NULL;
}

const char *redo_desc(void)
{
    return nredo ? redo_stack[nredo - 1].desc : NULL;
}
