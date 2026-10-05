/*
 * eclass.c - entity definitions (Hammer .fgd and Radiant .def files)
 */
#include "eclass.h"
#include "lexer.h"

#include <ctype.h>

static eclass_t **classes;
static int nclasses, maxclasses;

static eclass_t unknown_point = {
    "(unknown point entity)", EC_POINT, { 1.0f, 0.0f, 1.0f }, 1,
    { -8, -8, -8 }, { 8, 8, 8 }, 1, NULL, NULL, 0, NULL, 0, 1
};
static eclass_t unknown_solid = {
    "(unknown brush entity)", EC_SOLID, { 0.5f, 0.6f, 0.9f }, 1,
    { 0, 0, 0 }, { 0, 0, 0 }, 0, NULL, NULL, 0, NULL, 0, 1
};

static void free_class(eclass_t *ec)
{
    int i;
    free(ec->desc);
    for (i = 0; i < ec->nbases; i++)
        free(ec->bases[i]);
    free(ec->bases);
    for (i = 0; i < ec->nprops; i++) {
        free(ec->props[i].help);
        free(ec->props[i].choices);
    }
    free(ec->props);
    free(ec);
}

void eclass_clear(void)
{
    int i;
    for (i = 0; i < nclasses; i++)
        free_class(classes[i]);
    free(classes);
    classes = NULL;
    nclasses = maxclasses = 0;
}

eclass_t *eclass_find(const char *name)
{
    int i;
    if (!name)
        return NULL;
    for (i = 0; i < nclasses; i++)
        if (str_ieq(classes[i]->name, name))
            return classes[i];
    return NULL;
}

int eclass_count(void)
{
    return nclasses;
}

eclass_t *eclass_get(int index)
{
    return (index >= 0 && index < nclasses) ? classes[index] : NULL;
}

const eclass_prop_t *eclass_find_prop(const eclass_t *ec, const char *key)
{
    int i;
    if (!ec)
        return NULL;
    for (i = 0; i < ec->nprops; i++)
        if (str_ieq(ec->props[i].name, key))
            return &ec->props[i];
    return NULL;
}

static void add_class(eclass_t *ec)
{
    eclass_t *old = eclass_find(ec->name);
    int i;

    if (old) {
        /* a later definition replaces an earlier one */
        for (i = 0; i < nclasses; i++)
            if (classes[i] == old) {
                free_class(old);
                classes[i] = ec;
                return;
            }
    }
    if (nclasses >= maxclasses) {
        maxclasses = maxclasses ? maxclasses * 2 : 128;
        classes = xrealloc(classes, sizeof(eclass_t *) * (size_t)maxclasses);
    }
    classes[nclasses++] = ec;
}

static eclass_prop_t *add_prop(eclass_t *ec)
{
    eclass_prop_t *p;
    ec->props = xrealloc(ec->props, sizeof(eclass_prop_t) * (size_t)(ec->nprops + 1));
    p = &ec->props[ec->nprops++];
    memset(p, 0, sizeof(*p));
    return p;
}

static void add_choice(eclass_prop_t *p, const char *value, const char *desc, int def)
{
    eclass_choice_t *c;
    p->choices = xrealloc(p->choices, sizeof(eclass_choice_t) * (size_t)(p->nchoices + 1));
    c = &p->choices[p->nchoices++];
    str_copy(c->value, value, sizeof(c->value));
    str_copy(c->desc, desc, sizeof(c->desc));
    c->def = def;
}

/* ------------------------------------------------------------------ */
/* FGD                                                                 */

/* reads a value: a word or one or more "strings" joined with + */
static int fgd_value(lexer_t *lx, char *out, size_t size)
{
    strbuf_t sb;

    if (!lex_next(lx))
        return 0;
    if (!lx->quoted) {
        str_copy(out, lx->tok, size);
        return 1;
    }
    sb_init(&sb);
    sb_append(&sb, lx->tok);
    while (lex_peek_is(lx, "+")) {
        lex_next(lx);
        if (!lex_next(lx))
            break;
        sb_append(&sb, lx->tok);
    }
    str_copy(out, sb.data ? sb.data : "", size);
    sb_free(&sb);
    return 1;
}

static char *fgd_value_dup(lexer_t *lx)
{
    char buf[4096];
    if (!fgd_value(lx, buf, sizeof(buf)))
        return xstrdup("");
    return xstrdup(buf);
}

/* collects the tokens between parentheses (the '(' was already read) */
static void fgd_args(lexer_t *lx, char *out, size_t size)
{
    strbuf_t sb;
    int depth = 1;

    sb_init(&sb);
    while (depth > 0 && lex_next(lx)) {
        if (lex_is(lx, "("))
            depth++;
        else if (lex_is(lx, ")")) {
            if (--depth == 0)
                break;
        }
        if (sb.len)
            sb_append(&sb, " ");
        sb_append(&sb, lx->tok);
    }
    str_copy(out, sb.data ? sb.data : "", size);
    sb_free(&sb);
}

static void fgd_skip_block(lexer_t *lx)
{
    int depth = 0;
    while (lex_next(lx)) {
        if (lex_is(lx, "[") || lex_is(lx, "("))
            depth++;
        else if (lex_is(lx, "]") || lex_is(lx, ")")) {
            if (--depth <= 0)
                return;
        }
    }
}

static void fgd_parse_props(lexer_t *lx, eclass_t *ec)
{
    while (lex_next(lx)) {
        eclass_prop_t *p;
        char field[1024];
        int idx;

        if (lex_is(lx, "]"))
            return;
        if (str_ieq(lx->tok, "input") || str_ieq(lx->tok, "output")) {
            char args[256];
            lex_next(lx);
            if (lex_peek_is(lx, "(")) {
                lex_next(lx);
                fgd_args(lx, args, sizeof(args));
            }
            if (lex_peek_is(lx, ":")) {
                lex_next(lx);
                fgd_value(lx, field, sizeof(field));
            }
            continue;
        }
        p = add_prop(ec);
        str_copy(p->name, lx->tok, sizeof(p->name));
        if (lex_peek_is(lx, "(")) {
            lex_next(lx);
            fgd_args(lx, p->type, sizeof(p->type));
        }
        while (lex_peek_is(lx, "readonly") || lex_peek_is(lx, "report"))
            lex_next(lx);
        for (idx = 0; lex_peek_is(lx, ":"); idx++) {
            lex_next(lx);
            if (lex_peek_is(lx, ":") || lex_peek_is(lx, "]") || lex_peek_is(lx, "="))
                field[0] = 0;
            else
                fgd_value(lx, field, sizeof(field));
            if (idx == 0)
                str_copy(p->desc, field, sizeof(p->desc));
            else if (idx == 1)
                str_copy(p->def, field, sizeof(p->def));
            else if (idx == 2 && field[0]) {
                free(p->help);
                p->help = xstrdup(field);
            }
        }
        if (lex_peek_is(lx, "=")) {
            lex_next(lx);
            if (!lex_next(lx) || !lex_is(lx, "["))
                continue;
            while (lex_next(lx) && !lex_is(lx, "]")) {
                char value[64], desc[256], def[32] = "0";
                str_copy(value, lx->tok, sizeof(value));
                desc[0] = 0;
                if (lex_peek_is(lx, ":")) {
                    lex_next(lx);
                    fgd_value(lx, desc, sizeof(desc));
                }
                if (lex_peek_is(lx, ":")) {
                    lex_next(lx);
                    fgd_value(lx, def, sizeof(def));
                }
                add_choice(p, value, desc, atoi(def) != 0);
            }
        }
    }
}

static void fgd_parse_class(lexer_t *lx, const char *type, const char *dir);

static int fgd_load_text(const char *text, const char *dir)
{
    lexer_t lx;
    int before = nclasses;

    lex_init(&lx, text, "@()[]=:,");
    while (lex_next(&lx)) {
        if (!lex_is(&lx, "@"))
            continue;
        if (!lex_next(&lx))
            break;
        {
            char type[64];
            str_copy(type, lx.tok, sizeof(type));
            fgd_parse_class(&lx, type, dir);
        }
    }
    return nclasses - before;
}

static void fgd_parse_class(lexer_t *lx, const char *type, const char *dir)
{
    eclass_t *ec;
    char args[1024];
    size_t tl = strlen(type);

    if (str_ieq(type, "include")) {
        char name[PATH_LEN], path[PATH_LEN];
        fgd_value(lx, name, sizeof(name));
        path_join(path, sizeof(path), dir, name);
        eclass_load(path);
        return;
    }
    if (tl < 5 || !str_ieq(type + tl - 5, "Class")) {
        /* @mapsize(...), @MaterialExclusion [...], @AutoVisGroup = "x" [...] */
        while (lex_peek_is(lx, "=") || lex_peek_is(lx, "(") || lex_peek_is(lx, "[")) {
            if (lex_peek_is(lx, "=")) {
                char tmp[256];
                lex_next(lx);
                fgd_value(lx, tmp, sizeof(tmp));
            } else {
                fgd_skip_block(lx);
            }
        }
        return;
    }

    ec = xcalloc(1, sizeof(eclass_t));
    ec->kind = str_ieq(type, "BaseClass") ? EC_BASE : str_ieq(type, "SolidClass") ? EC_SOLID : EC_POINT;
    while (lex_next(lx)) {
        char helper[64];
        if (lex_is(lx, "="))
            break;
        str_copy(helper, lx->tok, sizeof(helper));
        args[0] = 0;
        if (lex_peek_is(lx, "(")) {
            lex_next(lx);
            fgd_args(lx, args, sizeof(args));
        }
        if (str_ieq(helper, "base")) {
            char *names[32];
            int n = str_split(args, ',', names, 32), i;
            for (i = 0; i < n; i++) {
                ec->bases = xrealloc(ec->bases, sizeof(char *) * (size_t)(ec->nbases + 1));
                ec->bases[ec->nbases++] = xstrdup(names[i]);
            }
        } else if (str_ieq(helper, "color")) {
            float r, g, b;
            if (sscanf(args, "%f %f %f", &r, &g, &b) == 3) {
                ec->color[0] = r / 255.0f;
                ec->color[1] = g / 255.0f;
                ec->color[2] = b / 255.0f;
                ec->has_color = 1;
            }
        } else if (str_ieq(helper, "size")) {
            double v[6];
            char *comma = strchr(args, ',');
            if (comma)
                *comma = ' ';
            if (sscanf(args, "%lf %lf %lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
                v3_set(ec->mins, v[0], v[1], v[2]);
                v3_set(ec->maxs, v[3], v[4], v[5]);
                ec->has_size = 1;
            } else if (sscanf(args, "%lf %lf %lf", &v[0], &v[1], &v[2]) == 3) {
                v3_set(ec->mins, -v[0] / 2, -v[1] / 2, -v[2] / 2);
                v3_set(ec->maxs, v[0] / 2, v[1] / 2, v[2] / 2);
                ec->has_size = 1;
            }
        }
    }
    if (!lex_next(lx)) {
        free_class(ec);
        return;
    }
    str_copy(ec->name, lx->tok, sizeof(ec->name));
    if (lex_peek_is(lx, ":")) {
        lex_next(lx);
        ec->desc = fgd_value_dup(lx);
    }
    if (lex_peek_is(lx, "[")) {
        lex_next(lx);
        fgd_parse_props(lx, ec);
    }
    add_class(ec);
}

/* ------------------------------------------------------------------ */
/* DEF (QuakeEd / Radiant comments)                                    */

static int def_load_text(const char *text)
{
    const char *p = text;
    int before = nclasses;

    while ((p = strstr(p, "/*QUAKED")) != NULL) {
        const char *end = strstr(p, "*/");
        const char *eol;
        char *line, *body;
        lexer_t lx;
        eclass_t *ec;
        eclass_prop_t *flags = NULL;
        int bit = 0;

        if (!end)
            break;
        p += 8;
        eol = strchr(p, '\n');
        if (!eol || eol > end)
            eol = end;
        line = xmalloc((size_t)(eol - p) + 1);
        memcpy(line, p, (size_t)(eol - p));
        line[eol - p] = 0;

        ec = xcalloc(1, sizeof(eclass_t));
        lex_init(&lx, line, "()?");
        if (!lex_next(&lx)) {
            free(line);
            free_class(ec);
            p = end;
            continue;
        }
        str_copy(ec->name, lx.tok, sizeof(ec->name));
        ec->kind = EC_POINT;
        if (lex_peek_is(&lx, "(")) {
            float c[3] = { 0, 0, 0 };
            int i;
            lex_next(&lx);
            for (i = 0; i < 3 && lex_next(&lx); i++)
                c[i] = (float)atof(lx.tok);
            lex_next(&lx);
            ec->color[0] = c[0];
            ec->color[1] = c[1];
            ec->color[2] = c[2];
            ec->has_color = 1;
        }
        if (lex_peek_is(&lx, "?")) {
            lex_next(&lx);
            ec->kind = EC_SOLID;
        } else if (lex_peek_is(&lx, "(")) {
            double v[6];
            int i;
            for (i = 0; i < 6; i++) {
                if (i == 0 || i == 3)
                    lex_next(&lx);    /* ( */
                lex_next(&lx);
                v[i] = atof(lx.tok);
                if (i == 2 || i == 5)
                    lex_next(&lx);    /* ) */
            }
            v3_set(ec->mins, v[0], v[1], v[2]);
            v3_set(ec->maxs, v[3], v[4], v[5]);
            ec->has_size = 1;
        }
        while (lex_next(&lx)) {
            char value[16];
            if (!flags) {
                flags = add_prop(ec);
                str_copy(flags->name, "spawnflags", sizeof(flags->name));
                str_copy(flags->type, "flags", sizeof(flags->type));
                str_copy(flags->desc, "Spawnflags", sizeof(flags->desc));
            }
            snprintf(value, sizeof(value), "%d", 1 << bit);
            if (strcmp(lx.tok, "-") && strcmp(lx.tok, "x"))
                add_choice(flags, value, lx.tok, 0);
            bit++;
        }
        free(line);

        body = xmalloc((size_t)(end - eol) + 1);
        memcpy(body, eol, (size_t)(end - eol));
        body[end - eol] = 0;
        ec->desc = xstrdup(str_trim(body));
        free(body);
        add_class(ec);
        p = end;
    }
    return nclasses - before;
}

/* ------------------------------------------------------------------ */

static void resolve(eclass_t *ec, int depth)
{
    eclass_prop_t *merged = NULL;
    int nmerged = 0, i, j, k;

    if (ec->resolved || depth > 16)
        return;
    ec->resolved = 1;
    for (i = 0; i < ec->nbases; i++) {
        eclass_t *base = eclass_find(ec->bases[i]);
        if (!base) {
            log_warn("entity class '%s': unknown base class '%s'", ec->name, ec->bases[i]);
            continue;
        }
        resolve(base, depth + 1);
        if (!ec->has_color && base->has_color) {
            memcpy(ec->color, base->color, sizeof(ec->color));
            ec->has_color = 1;
        }
        if (!ec->has_size && base->has_size) {
            v3_copy(base->mins, ec->mins);
            v3_copy(base->maxs, ec->maxs);
            ec->has_size = 1;
        }
        for (j = 0; j < base->nprops; j++) {
            int dup = 0;
            for (k = 0; k < nmerged && !dup; k++)
                dup = str_ieq(merged[k].name, base->props[j].name);
            for (k = 0; k < ec->nprops && !dup; k++) {
                eclass_prop_t *own = &ec->props[k];
                const eclass_prop_t *bp = &base->props[j];
                int c, d;
                if (!str_ieq(own->name, bp->name))
                    continue;
                dup = 1;
                /* flags are merged with the inherited ones (as Hammer does) */
                if (str_ieq(own->type, "flags") && str_ieq(bp->type, "flags"))
                    for (c = 0; c < bp->nchoices; c++) {
                        int have = 0;
                        for (d = 0; d < own->nchoices && !have; d++)
                            have = !strcmp(own->choices[d].value, bp->choices[c].value);
                        if (!have)
                            add_choice(own, bp->choices[c].value, bp->choices[c].desc, bp->choices[c].def);
                    }
            }
            if (dup)
                continue;
            merged = xrealloc(merged, sizeof(eclass_prop_t) * (size_t)(nmerged + 1));
            merged[nmerged] = base->props[j];
            merged[nmerged].help = base->props[j].help ? xstrdup(base->props[j].help) : NULL;
            if (base->props[j].nchoices) {
                size_t sz = sizeof(eclass_choice_t) * (size_t)base->props[j].nchoices;
                merged[nmerged].choices = xmalloc(sz);
                memcpy(merged[nmerged].choices, base->props[j].choices, sz);
            }
            nmerged++;
        }
    }
    if (nmerged) {
        merged = xrealloc(merged, sizeof(eclass_prop_t) * (size_t)(nmerged + ec->nprops));
        if (ec->nprops)
            memcpy(merged + nmerged, ec->props, sizeof(eclass_prop_t) * (size_t)ec->nprops);
        free(ec->props);
        ec->props = merged;
        ec->nprops += nmerged;
    }
    if (!ec->has_color) {
        ec->color[0] = ec->kind == EC_SOLID ? 0.5f : 1.0f;
        ec->color[1] = ec->kind == EC_SOLID ? 0.6f : 0.0f;
        ec->color[2] = ec->kind == EC_SOLID ? 0.9f : 1.0f;
    }
    if (!ec->has_size && ec->kind == EC_POINT) {
        v3_set(ec->mins, -8, -8, -8);
        v3_set(ec->maxs, 8, 8, 8);
        ec->has_size = 1;
    }
}

int eclass_load(const char *path)
{
    size_t len;
    char *text = file_read_all(path, &len);
    char dir[PATH_LEN];
    int n, i;

    if (!text) {
        log_warn("cannot read entity definitions '%s'", path);
        return 0;
    }
    path_dirname(dir, sizeof(dir), path);
    if (str_ieq(path_ext(path), ".def"))
        n = def_load_text(text);
    else
        n = fgd_load_text(text, dir);
    free(text);
    for (i = 0; i < nclasses; i++)
        classes[i]->resolved = 0;
    for (i = 0; i < nclasses; i++)
        resolve(classes[i], 0);
    log_info("loaded %d entity definitions from %s", n, path);
    return n;
}

void eclass_bind(entity_t *e)
{
    eclass_t *ec = eclass_find(ent_classname(e));
    if (!ec || ec->kind == EC_BASE)
        ec = e->brushes || ent_is_world(e) ? &unknown_solid : &unknown_point;
    e->ec = ec;
}

void eclass_bind_map(map_t *m)
{
    entity_t *e;
    for (e = m->entities; e; e = e->next)
        eclass_bind(e);
}

void ent_bounds(const entity_t *e, vec3_t mins, vec3_t maxs)
{
    const brush_t *b;

    if (ent_is_point(e)) {
        const eclass_t *ec = e->ec;
        if (ec && ec->has_size) {
            v3_add(e->origin, ec->mins, mins);
            v3_add(e->origin, ec->maxs, maxs);
        } else {
            v3_set(mins, e->origin[0] - 8, e->origin[1] - 8, e->origin[2] - 8);
            v3_set(maxs, e->origin[0] + 8, e->origin[1] + 8, e->origin[2] + 8);
        }
        return;
    }
    bounds_clear(mins, maxs);
    for (b = e->brushes; b; b = b->next) {
        bounds_add(mins, maxs, b->mins);
        bounds_add(mins, maxs, b->maxs);
    }
    if (!bounds_valid(mins, maxs)) {
        v3_clear(mins);
        v3_clear(maxs);
    }
}

void ent_color(const entity_t *e, float out[3])
{
    const eclass_t *ec = e->ec;
    if (ec) {
        out[0] = ec->color[0];
        out[1] = ec->color[1];
        out[2] = ec->color[2];
    } else {
        out[0] = 1.0f;
        out[1] = 0.0f;
        out[2] = 1.0f;
    }
}
