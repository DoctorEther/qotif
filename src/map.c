/*
 * map.c - entities, brushes and faces; reading and writing .map files
 */
#include "map.h"
#include "brush.h"
#include "lexer.h"

char map_game_name[64] = "Quake";

/* ------------------------------------------------------------------ */
/* entities                                                            */

entity_t *entity_new(void)
{
    return xcalloc(1, sizeof(entity_t));
}

void entity_free(entity_t *e)
{
    int i;
    brush_t *b, *next;

    if (!e)
        return;
    for (i = 0; i < e->npairs; i++) {
        free(e->pairs[i].key);
        free(e->pairs[i].value);
    }
    free(e->pairs);
    for (b = e->brushes; b; b = next) {
        next = b->next;
        brush_free(b);
    }
    free(e);
}

const char *ent_get(const entity_t *e, const char *key)
{
    int i;
    for (i = 0; i < e->npairs; i++)
        if (!strcmp(e->pairs[i].key, key))
            return e->pairs[i].value;
    return NULL;
}

static void parse_origin(entity_t *e, const char *value)
{
    double x = 0, y = 0, z = 0;
    if (value)
        sscanf(value, "%lf %lf %lf", &x, &y, &z);
    v3_set(e->origin, x, y, z);
}

void ent_set(entity_t *e, const char *key, const char *value)
{
    int i;

    if (!strcmp(key, "origin"))
        parse_origin(e, value);
    for (i = 0; i < e->npairs; i++) {
        if (!strcmp(e->pairs[i].key, key)) {
            free(e->pairs[i].value);
            e->pairs[i].value = xstrdup(value);
            return;
        }
    }
    if (e->npairs >= e->maxpairs) {
        e->maxpairs = e->maxpairs ? e->maxpairs * 2 : 8;
        e->pairs = xrealloc(e->pairs, sizeof(epair_t) * (size_t)e->maxpairs);
    }
    e->pairs[e->npairs].key = xstrdup(key);
    e->pairs[e->npairs].value = xstrdup(value);
    e->npairs++;
}

void ent_remove(entity_t *e, const char *key)
{
    int i;
    for (i = 0; i < e->npairs; i++) {
        if (!strcmp(e->pairs[i].key, key)) {
            free(e->pairs[i].key);
            free(e->pairs[i].value);
            memmove(&e->pairs[i], &e->pairs[i + 1], sizeof(epair_t) * (size_t)(e->npairs - i - 1));
            e->npairs--;
            if (!strcmp(key, "origin"))
                v3_clear(e->origin);
            return;
        }
    }
}

const char *ent_classname(const entity_t *e)
{
    const char *c = ent_get(e, "classname");
    return c ? c : "";
}

int ent_is_world(const entity_t *e)
{
    return !strcmp(ent_classname(e), "worldspawn");
}

int ent_is_point(const entity_t *e)
{
    return !e->brushes && !ent_is_world(e);
}

void ent_set_origin(entity_t *e, const vec3_t o)
{
    char x[32], y[32], z[32], buf[128];
    fmt_num(x, sizeof(x), o[0]);
    fmt_num(y, sizeof(y), o[1]);
    fmt_num(z, sizeof(z), o[2]);
    snprintf(buf, sizeof(buf), "%s %s %s", x, y, z);
    ent_set(e, "origin", buf);
}

void ent_add_brush(entity_t *e, brush_t *b)
{
    b->owner = e;
    b->next = NULL;
    b->prev = e->btail;
    if (e->btail)
        e->btail->next = b;
    else
        e->brushes = b;
    e->btail = b;
    e->nbrushes++;
}

void ent_unlink_brush(entity_t *e, brush_t *b)
{
    if (b->prev)
        b->prev->next = b->next;
    else
        e->brushes = b->next;
    if (b->next)
        b->next->prev = b->prev;
    else
        e->btail = b->prev;
    b->prev = b->next = NULL;
    b->owner = NULL;
    e->nbrushes--;
}

/* ------------------------------------------------------------------ */
/* map                                                                 */

static map_t *map_create_empty(void)
{
    return xcalloc(1, sizeof(map_t));
}

map_t *map_create(void)
{
    map_t *m = map_create_empty();
    entity_t *w = entity_new();
    ent_set(w, "classname", "worldspawn");
    map_add_entity(m, w);
    m->world = w;
    return m;
}

void map_free(map_t *m)
{
    entity_t *e, *next;
    if (!m)
        return;
    for (e = m->entities; e; e = next) {
        next = e->next;
        entity_free(e);
    }
    free(m);
}

void map_add_entity(map_t *m, entity_t *e)
{
    e->next = NULL;
    e->prev = m->etail;
    if (m->etail)
        m->etail->next = e;
    else
        m->entities = e;
    m->etail = e;
}

void map_unlink_entity(map_t *m, entity_t *e)
{
    if (e->prev)
        e->prev->next = e->next;
    else
        m->entities = e->next;
    if (e->next)
        e->next->prev = e->prev;
    else
        m->etail = e->prev;
    e->prev = e->next = NULL;
}

int map_count_brushes(const map_t *m)
{
    const entity_t *e;
    int n = 0;
    for (e = m->entities; e; e = e->next)
        n += e->nbrushes;
    return n;
}

int map_count_entities(const map_t *m)
{
    const entity_t *e;
    int n = 0;
    for (e = m->entities; e; e = e->next)
        n++;
    return n;
}

void map_build_all(map_t *m)
{
    entity_t *e;
    brush_t *b;
    for (e = m->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next)
            brush_build(b);
}

/* ------------------------------------------------------------------ */
/* brushes and faces                                                   */

brush_t *brush_new(void)
{
    return xcalloc(1, sizeof(brush_t));
}

void face_free_data(face_t *f)
{
    if (f->w)
        winding_free(f->w);
    free(f->extra);
    f->w = NULL;
    f->extra = NULL;
}

void brush_free(brush_t *b)
{
    int i;
    if (!b)
        return;
    for (i = 0; i < b->nfaces; i++)
        face_free_data(&b->faces[i]);
    free(b->faces);
    free(b);
}

void face_copy(face_t *dst, const face_t *src)
{
    *dst = *src;
    dst->w = winding_copy(src->w);
    dst->extra = src->extra ? xstrdup(src->extra) : NULL;
}

void brush_copy_faces(brush_t *dst, const brush_t *src)
{
    int i;
    for (i = 0; i < dst->nfaces; i++)
        face_free_data(&dst->faces[i]);
    if (dst->maxfaces < src->nfaces) {
        dst->maxfaces = src->nfaces;
        dst->faces = xrealloc(dst->faces, sizeof(face_t) * (size_t)dst->maxfaces);
    }
    for (i = 0; i < src->nfaces; i++)
        face_copy(&dst->faces[i], &src->faces[i]);
    dst->nfaces = src->nfaces;
    v3_copy(src->mins, dst->mins);
    v3_copy(src->maxs, dst->maxs);
    dst->valid = src->valid;
}

brush_t *brush_copy(const brush_t *b)
{
    brush_t *c = brush_new();
    brush_copy_faces(c, b);
    c->selected = b->selected;
    return c;
}

face_t *brush_add_face(brush_t *b)
{
    face_t *f;
    if (b->nfaces >= b->maxfaces) {
        b->maxfaces = b->maxfaces ? b->maxfaces * 2 : 8;
        b->faces = xrealloc(b->faces, sizeof(face_t) * (size_t)b->maxfaces);
    }
    f = &b->faces[b->nfaces++];
    memset(f, 0, sizeof(*f));
    f->scale[0] = f->scale[1] = 1.0;
    return f;
}

void brush_remove_face(brush_t *b, int index)
{
    if (index < 0 || index >= b->nfaces)
        return;
    face_free_data(&b->faces[index]);
    memmove(&b->faces[index], &b->faces[index + 1], sizeof(face_t) * (size_t)(b->nfaces - index - 1));
    b->nfaces--;
}

/* ------------------------------------------------------------------ */
/* parsing                                                             */

static int parse_number(lexer_t *lx, double *out, char *err, size_t errsize)
{
    char *end;
    if (!lex_next(lx)) {
        snprintf(err, errsize, "line %d: unexpected end of file", lx->line);
        return 0;
    }
    *out = strtod(lx->tok, &end);
    if (end == lx->tok || *end) {
        snprintf(err, errsize, "line %d: expected a number, got '%s'", lx->line, lx->tok);
        return 0;
    }
    return 1;
}

static int expect(lexer_t *lx, const char *s, char *err, size_t errsize)
{
    if (!lex_next(lx) || !lex_is(lx, s)) {
        snprintf(err, errsize, "line %d: expected '%s', got '%s'", lx->line, s, lx->tok);
        return 0;
    }
    return 1;
}

static int parse_face(lexer_t *lx, brush_t *b, char *err, size_t errsize)
{
    face_t *f = brush_add_face(b);
    int i, j;

    /* the first '(' was consumed by the caller */
    for (i = 0; i < 3; i++) {
        if (i > 0 && !expect(lx, "(", err, errsize))
            return 0;
        for (j = 0; j < 3; j++)
            if (!parse_number(lx, &f->pts[i][j], err, errsize))
                return 0;
        if (!expect(lx, ")", err, errsize))
            return 0;
    }
    if (!lex_word(lx)) {
        snprintf(err, errsize, "line %d: missing texture name", lx->line);
        return 0;
    }
    str_copy(f->tex, lx->tok, sizeof(f->tex));

    if (lex_peek_is(lx, "[")) {
        f->valve = 1;
        for (i = 0; i < 2; i++) {
            double *axis = i == 0 ? f->uaxis : f->vaxis;
            if (!expect(lx, "[", err, errsize))
                return 0;
            for (j = 0; j < 3; j++)
                if (!parse_number(lx, &axis[j], err, errsize))
                    return 0;
            if (!parse_number(lx, &f->shift[i], err, errsize))
                return 0;
            if (!expect(lx, "]", err, errsize))
                return 0;
        }
    } else {
        if (!parse_number(lx, &f->shift[0], err, errsize) ||
            !parse_number(lx, &f->shift[1], err, errsize))
            return 0;
    }
    if (!parse_number(lx, &f->rotate, err, errsize) ||
        !parse_number(lx, &f->scale[0], err, errsize) ||
        !parse_number(lx, &f->scale[1], err, errsize))
        return 0;
    if (f->scale[0] == 0.0)
        f->scale[0] = 1.0;
    if (f->scale[1] == 0.0)
        f->scale[1] = 1.0;

    if (lex_more_on_line(lx)) {
        strbuf_t sb;
        sb_init(&sb);
        while (lex_more_on_line(lx) && lex_word(lx)) {
            if (sb.len)
                sb_append(&sb, " ");
            sb_append(&sb, lx->tok);
        }
        f->extra = sb_steal(&sb);
    }
    return 1;
}

static brush_t *parse_brush(lexer_t *lx, char *err, size_t errsize)
{
    brush_t *b = brush_new();

    for (;;) {
        if (!lex_next(lx)) {
            snprintf(err, errsize, "line %d: unexpected end of file inside brush", lx->line);
            break;
        }
        if (lex_is(lx, "}"))
            return b;
        if (lex_is(lx, "(")) {
            if (!parse_face(lx, b, err, errsize))
                break;
            continue;
        }
        snprintf(err, errsize, "line %d: unsupported brush syntax '%s' (only Quake and Valve 220 brushes)",
                 lx->line, lx->tok);
        break;
    }
    brush_free(b);
    return NULL;
}

map_t *map_parse(const char *text, char *err, size_t errsize)
{
    map_t *m = map_create_empty();
    lexer_t lx;
    entity_t *e, *world = NULL;
    brush_t *b;
    char dummy[8];
    const char *mv;

    if (!err) {
        err = dummy;
        errsize = sizeof(dummy);
    }
    err[0] = 0;
    lex_init(&lx, text, "{}()[]");
    while (lex_next(&lx)) {
        if (!lex_is(&lx, "{")) {
            snprintf(err, errsize, "line %d: expected '{', got '%s'", lx.line, lx.tok);
            goto fail;
        }
        e = entity_new();
        map_add_entity(m, e);
        for (;;) {
            if (!lex_next(&lx)) {
                snprintf(err, errsize, "line %d: unexpected end of file inside entity", lx.line);
                goto fail;
            }
            if (lex_is(&lx, "}"))
                break;
            if (lex_is(&lx, "{")) {
                b = parse_brush(&lx, err, errsize);
                if (!b)
                    goto fail;
                ent_add_brush(e, b);
                continue;
            }
            if (lx.quoted) {
                char key[256];
                str_copy(key, lx.tok, sizeof(key));
                if (!lex_next(&lx) || !lx.quoted) {
                    snprintf(err, errsize, "line %d: missing value for key '%s'", lx.line, key);
                    goto fail;
                }
                ent_set(e, key, lx.tok);
                continue;
            }
            snprintf(err, errsize, "line %d: unexpected '%s'", lx.line, lx.tok);
            goto fail;
        }
    }

    for (e = m->entities; e; e = e->next)
        if (ent_is_world(e)) {
            world = e;
            break;
        }
    if (!world) {
        world = entity_new();
        ent_set(world, "classname", "worldspawn");
        map_add_entity(m, world);
    }
    if (m->entities != world) {
        map_unlink_entity(m, world);
        world->next = m->entities;
        world->prev = NULL;
        m->entities->prev = world;
        m->entities = world;
    }
    m->world = world;

    mv = ent_get(world, "mapversion");
    if (mv && atoi(mv) == 220)
        m->valve220 = 1;
    for (e = m->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next) {
            if (b->nfaces && b->faces[0].valve)
                m->valve220 = 1;
            brush_build(b);
        }
    return m;

fail:
    map_free(m);
    return NULL;
}

map_t *map_load(const char *path, char *err, size_t errsize)
{
    size_t len;
    char *text = file_read_all(path, &len);
    map_t *m;

    if (!text) {
        snprintf(err, errsize, "cannot read '%s'", path);
        return NULL;
    }
    m = map_parse(text, err, errsize);
    free(text);
    if (m)
        str_copy(m->path, path, sizeof(m->path));
    return m;
}

/* ------------------------------------------------------------------ */
/* writing                                                             */

static void write_vec(strbuf_t *sb, const double *v, int n)
{
    char buf[48];
    int i;
    for (i = 0; i < n; i++) {
        fmt_num(buf, sizeof(buf), v[i]);
        sb_append(sb, buf);
        if (i < n - 1)
            sb_append(sb, " ");
    }
}

static void write_face(strbuf_t *sb, const face_t *src, int valve)
{
    face_t f = *src;
    char buf[48];
    int i;

    if (valve && !f.valve)
        face_to_valve(&f);
    for (i = 0; i < 3; i++) {
        sb_append(sb, "( ");
        write_vec(sb, f.pts[i], 3);
        sb_append(sb, " ) ");
    }
    sb_append(sb, f.tex[0] ? f.tex : "__TB_empty");
    if (valve) {
        sb_append(sb, " [ ");
        write_vec(sb, f.uaxis, 3);
        fmt_num(buf, sizeof(buf), f.shift[0]);
        sb_appendf(sb, " %s ] [ ", buf);
        write_vec(sb, f.vaxis, 3);
        fmt_num(buf, sizeof(buf), f.shift[1]);
        sb_appendf(sb, " %s ] ", buf);
    } else {
        sb_append(sb, " ");
        write_vec(sb, f.shift, 2);
        sb_append(sb, " ");
    }
    fmt_num(buf, sizeof(buf), f.rotate);
    sb_append(sb, buf);
    sb_append(sb, " ");
    write_vec(sb, f.scale, 2);
    if (f.extra) {
        sb_append(sb, " ");
        sb_append(sb, f.extra);
    }
    sb_append(sb, "\n");
}

static void write_entity(strbuf_t *sb, const map_t *m, const entity_t *e, int index, int selected_only)
{
    const brush_t *b;
    int i, n = 0;

    sb_appendf(sb, "// entity %d\n{\n", index);
    if (selected_only && e == m->world) {
        sb_append(sb, "\"classname\" \"worldspawn\"\n");
        if (m->valve220)
            sb_append(sb, "\"mapversion\" \"220\"\n");
    } else {
        for (i = 0; i < e->npairs; i++)
            sb_appendf(sb, "\"%s\" \"%s\"\n", e->pairs[i].key, e->pairs[i].value);
    }
    for (b = e->brushes; b; b = b->next) {
        if (selected_only && !b->selected)
            continue;
        sb_appendf(sb, "// brush %d\n{\n", n++);
        for (i = 0; i < b->nfaces; i++)
            write_face(sb, &b->faces[i], m->valve220);
        sb_append(sb, "}\n");
    }
    sb_append(sb, "}\n");
}

static int entity_has_selection(const entity_t *e)
{
    const brush_t *b;
    if (ent_is_point(e))
        return e->selected;
    for (b = e->brushes; b; b = b->next)
        if (b->selected)
            return 1;
    return 0;
}

char *map_write_string(const map_t *m, int selected_only)
{
    strbuf_t sb;
    const entity_t *e;
    int index = 0;

    sb_init(&sb);
    sb_appendf(&sb, "// Game: %s\n// Format: %s\n", map_game_name, m->valve220 ? "Valve" : "Standard");
    for (e = m->entities; e; e = e->next) {
        if (selected_only && e != m->world && !entity_has_selection(e))
            continue;
        write_entity(&sb, m, e, index++, selected_only);
    }
    return sb_steal(&sb);
}

int map_save(const map_t *m, const char *path)
{
    char *text = map_write_string(m, 0);
    char tmp[PATH_LEN + 8];
    int ok;

    /* write to a temporary file first so a failed save never truncates the map */
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    ok = file_write_all(tmp, text, strlen(text));
    free(text);
    if (ok && rename(tmp, path) != 0) {
        remove(tmp);
        ok = 0;
    }
    return ok;
}
