/*
 * brush.c - brush geometry, primitives, transforms, CSG and texturing
 */
#include "brush.h"

#define CLIP_EPS 0.001

/* ------------------------------------------------------------------ */
/* building                                                            */

int brush_build(brush_t *b)
{
    int i, j, k, n = 0;

    bounds_clear(b->mins, b->maxs);
    for (i = 0; i < b->nfaces; i++) {
        face_t *f = &b->faces[i];
        if (f->w) {
            winding_free(f->w);
            f->w = NULL;
        }
        f->planeok = plane_from_points(&f->plane, f->pts[0], f->pts[1], f->pts[2]);
    }
    for (i = 0; i < b->nfaces; i++) {
        face_t *f = &b->faces[i];
        winding_t *w;

        if (!f->planeok)
            continue;
        w = winding_base(&f->plane);
        for (j = 0; j < b->nfaces && w; j++) {
            face_t *o = &b->faces[j];
            if (j == i || !o->planeok)
                continue;
            if (plane_equal(&f->plane, &o->plane)) {
                /* duplicate plane: only the first copy gets a polygon */
                if (j < i) {
                    winding_free(w);
                    w = NULL;
                }
                continue;
            }
            w = winding_clip(w, &o->plane, CLIP_EPS);
        }
        if (w && (w->numpoints < 3 || winding_area(w) < 0.001)) {
            winding_free(w);
            w = NULL;
        }
        f->w = w;
        if (w) {
            n++;
            for (k = 0; k < w->numpoints; k++)
                bounds_add(b->mins, b->maxs, w->p[k]);
        }
    }
    b->valid = n >= 4;
    if (!b->valid && n == 0) {
        v3_clear(b->mins);
        v3_clear(b->maxs);
    }
    return b->valid;
}

int brush_finalize(brush_t *b)
{
    int i;

    brush_build(b);
    for (i = b->nfaces - 1; i >= 0; i--)
        if (!b->faces[i].w)
            brush_remove_face(b, i);
    for (i = 0; i < b->nfaces; i++)
        face_set_points_from_winding(&b->faces[i]);
    brush_build(b);
    for (i = 0; i < b->nfaces; i++)
        if (b->faces[i].valve)
            face_fix_axes(&b->faces[i]);
    return b->valid;
}

void face_points_from_plane(face_t *f, const plane_t *pl)
{
    winding_t *w = winding_base(pl);
    int i;
    for (i = 0; i < 3; i++)
        v3_copy(w->p[i], f->pts[i]);
    winding_free(w);
}

void face_set_points_from_winding(face_t *f)
{
    const winding_t *w = f->w;
    int i, a = 0, b = 1, c = 2, tmp;
    double best = -1.0;
    vec3_t pts[3], d1, d2, cr;
    plane_t pl;

    if (!w || w->numpoints < 3)
        return;
    /* pick the farthest point from p0, then the one giving the largest triangle */
    for (i = 1; i < w->numpoints; i++) {
        double d;
        v3_sub(w->p[i], w->p[0], d1);
        d = v3_dot(d1, d1);
        if (d > best) {
            best = d;
            b = i;
        }
    }
    best = -1.0;
    for (i = 1; i < w->numpoints; i++) {
        double d;
        if (i == b)
            continue;
        v3_sub(w->p[b], w->p[0], d1);
        v3_sub(w->p[i], w->p[0], d2);
        v3_cross(d1, d2, cr);
        d = v3_dot(cr, cr);
        if (d > best) {
            best = d;
            c = i;
        }
    }
    /* keep the winding order so the normal keeps pointing outwards */
    if (b > c) {
        tmp = b;
        b = c;
        c = tmp;
    }
    v3_copy(w->p[a], pts[0]);
    v3_copy(w->p[b], pts[1]);
    v3_copy(w->p[c], pts[2]);
    for (i = 0; i < 3; i++) {
        pts[i][0] = round_if_near(pts[i][0], 1e-4);
        pts[i][1] = round_if_near(pts[i][1], 1e-4);
        pts[i][2] = round_if_near(pts[i][2], 1e-4);
    }
    if (!plane_from_points(&pl, pts[0], pts[1], pts[2]) || v3_dot(pl.normal, f->plane.normal) < 0.9999)
        return;
    for (i = 0; i < 3; i++)
        v3_copy(pts[i], f->pts[i]);
}

/* ------------------------------------------------------------------ */
/* texturing                                                           */

static const double baseaxis[18][3] = {
    { 0, 0, 1 }, { 1, 0, 0 }, { 0, -1, 0 },   /* floor */
    { 0, 0, -1 }, { 1, 0, 0 }, { 0, -1, 0 },  /* ceiling */
    { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 },   /* west wall */
    { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 },  /* east wall */
    { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 },   /* south wall */
    { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }   /* north wall */
};

void face_texture_axis(const vec3_t normal, vec3_t xv, vec3_t yv)
{
    int i, bestaxis = 0;
    double best = 0.0;

    for (i = 0; i < 6; i++) {
        double d = v3_dot(normal, baseaxis[i * 3]);
        if (d > best) {
            best = d;
            bestaxis = i;
        }
    }
    v3_copy(baseaxis[bestaxis * 3 + 1], xv);
    v3_copy(baseaxis[bestaxis * 3 + 2], yv);
}

void face_texture_vecs(const face_t *f, double vecs[2][4])
{
    vec3_t axes[2];
    double sinv, cosv, ang;
    int i, j, sv, tv;

    if (f->valve) {
        double sx = f->scale[0] ? f->scale[0] : 1.0;
        double sy = f->scale[1] ? f->scale[1] : 1.0;
        for (j = 0; j < 3; j++) {
            vecs[0][j] = f->uaxis[j] / sx;
            vecs[1][j] = f->vaxis[j] / sy;
        }
        vecs[0][3] = f->shift[0];
        vecs[1][3] = f->shift[1];
        return;
    }

    face_texture_axis(f->plane.normal, axes[0], axes[1]);
    ang = f->rotate;
    if (ang == 0.0) {
        sinv = 0.0; cosv = 1.0;
    } else if (ang == 90.0) {
        sinv = 1.0; cosv = 0.0;
    } else if (ang == 180.0) {
        sinv = 0.0; cosv = -1.0;
    } else if (ang == 270.0) {
        sinv = -1.0; cosv = 0.0;
    } else {
        sinv = sin(DEG2RAD(ang));
        cosv = cos(DEG2RAD(ang));
    }
    sv = axes[0][0] ? 0 : (axes[0][1] ? 1 : 2);
    tv = axes[1][0] ? 0 : (axes[1][1] ? 1 : 2);
    for (i = 0; i < 2; i++) {
        double ns = cosv * axes[i][sv] - sinv * axes[i][tv];
        double nt = sinv * axes[i][sv] + cosv * axes[i][tv];
        axes[i][sv] = ns;
        axes[i][tv] = nt;
    }
    for (i = 0; i < 2; i++) {
        double s = f->scale[i] ? f->scale[i] : 1.0;
        for (j = 0; j < 3; j++)
            vecs[i][j] = axes[i][j] / s;
        vecs[i][3] = f->shift[i];
    }
}

void face_to_valve(face_t *f)
{
    double vecs[2][4];
    int j;
    double sx, sy;

    if (f->valve)
        return;
    face_texture_vecs(f, vecs);
    sx = f->scale[0] ? f->scale[0] : 1.0;
    sy = f->scale[1] ? f->scale[1] : 1.0;
    for (j = 0; j < 3; j++) {
        f->uaxis[j] = vecs[0][j] * sx;
        f->vaxis[j] = vecs[1][j] * sy;
    }
    f->valve = 1;
}

void face_fix_axes(face_t *f)
{
    vec3_t c;
    v3_cross(f->uaxis, f->vaxis, c);
    if (v3_len(f->uaxis) < 0.5 || v3_len(f->vaxis) < 0.5 || fabs(v3_dot(c, f->plane.normal)) < 0.01)
        face_texture_axis(f->plane.normal, f->uaxis, f->vaxis);
}

void face_init_texture(face_t *f, const char *tex, int valve)
{
    str_copy(f->tex, tex ? tex : "", sizeof(f->tex));
    f->shift[0] = f->shift[1] = 0.0;
    f->rotate = 0.0;
    f->scale[0] = f->scale[1] = 1.0;
    f->valve = valve;
    if (valve)
        face_texture_axis(f->plane.normal, f->uaxis, f->vaxis);
}

void face_fit_texture(face_t *f, int texw, int texh)
{
    double vecs[2][4];
    double smin = 1e30, smax = -1e30, tmin = 1e30, tmax = -1e30;
    int i;

    if (!f->w || texw <= 0 || texh <= 0)
        return;
    f->scale[0] = f->scale[1] = 1.0;
    f->shift[0] = f->shift[1] = 0.0;
    face_texture_vecs(f, vecs);
    for (i = 0; i < f->w->numpoints; i++) {
        double s = v3_dot(f->w->p[i], vecs[0]);
        double t = v3_dot(f->w->p[i], vecs[1]);
        if (s < smin) smin = s;
        if (s > smax) smax = s;
        if (t < tmin) tmin = t;
        if (t > tmax) tmax = t;
    }
    if (smax - smin < 0.001 || tmax - tmin < 0.001)
        return;
    f->scale[0] = (smax - smin) / texw;
    f->scale[1] = (tmax - tmin) / texh;
    f->shift[0] = -smin / f->scale[0];
    f->shift[1] = -tmin / f->scale[1];
}

/* ------------------------------------------------------------------ */
/* construction                                                        */

face_t *brush_add_plane_face(brush_t *b, const plane_t *pl, const face_t *texsrc,
                             const char *tex, int valve)
{
    face_t *f = brush_add_face(b);

    face_points_from_plane(f, pl);
    f->plane = *pl;
    f->planeok = 1;
    if (texsrc) {
        str_copy(f->tex, texsrc->tex, sizeof(f->tex));
        f->shift[0] = texsrc->shift[0];
        f->shift[1] = texsrc->shift[1];
        f->rotate = texsrc->rotate;
        f->scale[0] = texsrc->scale[0];
        f->scale[1] = texsrc->scale[1];
        v3_copy(texsrc->uaxis, f->uaxis);
        v3_copy(texsrc->vaxis, f->vaxis);
        f->valve = texsrc->valve;
        if (f->valve)
            face_fix_axes(f);
    } else {
        face_init_texture(f, tex, valve);
    }
    return f;
}

brush_t *brush_from_bounds(const vec3_t mins, const vec3_t maxs, const char *tex, int valve)
{
    brush_t *b = brush_new();
    plane_t pl;
    int i;

    for (i = 0; i < 3; i++) {
        v3_clear(pl.normal);
        pl.normal[i] = 1.0;
        pl.dist = maxs[i];
        brush_add_plane_face(b, &pl, NULL, tex, valve);
        pl.normal[i] = -1.0;
        pl.dist = -mins[i];
        brush_add_plane_face(b, &pl, NULL, tex, valve);
    }
    if (!brush_finalize(b)) {
        brush_free(b);
        return NULL;
    }
    return b;
}

/* convex hull of a point cloud (small n; brute force over triangles) */
brush_t *brush_from_points(vec3_t *pts, int n, const char *tex, int valve)
{
    brush_t *b = brush_new();
    plane_t *planes = NULL;
    int nplanes = 0, maxplanes = 0;
    int i, j, k, l;

    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            for (k = j + 1; k < n; k++) {
                plane_t pl;
                int front = 0, back = 0, dup = 0;

                if (!plane_from_points(&pl, pts[i], pts[j], pts[k]))
                    continue;
                for (l = 0; l < n; l++) {
                    double d = plane_dist(&pl, pts[l]);
                    if (d > 0.01)
                        front++;
                    else if (d < -0.01)
                        back++;
                }
                if (front && back)
                    continue;
                if (!front && !back)
                    continue;
                if (front)
                    plane_flip(&pl, &pl);
                for (l = 0; l < nplanes; l++)
                    if (v3_dot(planes[l].normal, pl.normal) > 0.99999 && fabs(planes[l].dist - pl.dist) < 0.01) {
                        dup = 1;
                        break;
                    }
                if (dup)
                    continue;
                if (nplanes >= maxplanes) {
                    maxplanes = maxplanes ? maxplanes * 2 : 16;
                    planes = xrealloc(planes, sizeof(plane_t) * (size_t)maxplanes);
                }
                planes[nplanes++] = pl;
            }

    for (i = 0; i < nplanes; i++)
        brush_add_plane_face(b, &planes[i], NULL, tex, valve);
    free(planes);
    if (nplanes < 4 || !brush_finalize(b)) {
        brush_free(b);
        return NULL;
    }
    return b;
}

brush_t *brush_primitive(int prim, int sides, int hax, int vax, int dax,
                         const vec3_t mins, const vec3_t maxs, const char *tex, int valve)
{
    vec3_t *pts;
    brush_t *b;
    int n = 0, i;

    if (sides < 3)
        sides = 3;
    if (sides > 32)
        sides = 32;
    if (prim == PRIM_BLOCK)
        return brush_from_bounds(mins, maxs, tex, valve);

    pts = xmalloc(sizeof(vec3_t) * (size_t)(sides * 2 + 8));
    if (prim == PRIM_WEDGE) {
        /* a ramp rising (along z) towards +h; extruded along the other
         * horizontal axis */
        int h = hax == 2 ? 0 : hax;
        int e = h == 0 ? 1 : 0;
        int ends;
        for (ends = 0; ends < 2; ends++) {
            double ev = ends ? maxs[e] : mins[e];
            v3_clear(pts[n]); pts[n][e] = ev; pts[n][h] = mins[h]; pts[n][2] = mins[2]; n++;
            v3_clear(pts[n]); pts[n][e] = ev; pts[n][h] = maxs[h]; pts[n][2] = mins[2]; n++;
            v3_clear(pts[n]); pts[n][e] = ev; pts[n][h] = maxs[h]; pts[n][2] = maxs[2]; n++;
        }
    } else {
        double ch = (mins[hax] + maxs[hax]) * 0.5, cv = (mins[vax] + maxs[vax]) * 0.5;
        double rh = (maxs[hax] - mins[hax]) * 0.5, rv = (maxs[vax] - mins[vax]) * 0.5;
        double offset = (sides % 2 == 0) ? Q_PI / sides : Q_PI / 2.0;

        for (i = 0; i < sides; i++) {
            double a = offset + 2.0 * Q_PI * i / sides;
            vec3_t p;
            p[hax] = floor(ch + cos(a) * rh + 0.5);
            p[vax] = floor(cv + sin(a) * rv + 0.5);
            p[dax] = mins[dax];
            v3_copy(p, pts[n++]);
            if (prim == PRIM_CYLINDER) {
                p[dax] = maxs[dax];
                v3_copy(p, pts[n++]);
            }
        }
        if (prim == PRIM_SPIKE) {
            vec3_t apex;
            apex[hax] = floor(ch + 0.5);
            apex[vax] = floor(cv + 0.5);
            apex[dax] = maxs[dax];
            v3_copy(apex, pts[n++]);
        }
    }
    b = brush_from_points(pts, n, tex, valve);
    free(pts);
    return b;
}

/* ------------------------------------------------------------------ */
/* transforms                                                          */

void brush_translate(brush_t *b, const vec3_t d, int texlock)
{
    int i, k;
    for (i = 0; i < b->nfaces; i++) {
        face_t *f = &b->faces[i];
        if (texlock) {
            double vecs[2][4];
            face_texture_vecs(f, vecs);
            f->shift[0] -= v3_dot(d, vecs[0]);
            f->shift[1] -= v3_dot(d, vecs[1]);
        }
        for (k = 0; k < 3; k++)
            v3_add(f->pts[k], d, f->pts[k]);
    }
    brush_build(b);
}

void brush_transform(brush_t *b, mat3_t m, const vec3_t t, int texlock)
{
    double det;
    int ortho, i, k;

    if (mat3_is_identity(m)) {
        brush_translate(b, t, texlock);
        return;
    }
    det = mat3_det(m);
    ortho = mat3_is_orthonormal(m);
    for (i = 0; i < b->nfaces; i++) {
        face_t *f = &b->faces[i];
        for (k = 0; k < 3; k++) {
            mat3_mul_vec(m, f->pts[k], f->pts[k]);
            v3_add(f->pts[k], t, f->pts[k]);
        }
        if (det < 0) {
            vec3_t tmp;
            v3_copy(f->pts[0], tmp);
            v3_copy(f->pts[2], f->pts[0]);
            v3_copy(tmp, f->pts[2]);
        }
        if (f->valve && texlock && ortho) {
            vec3_t u2, v2;
            double sx = f->scale[0] ? f->scale[0] : 1.0;
            double sy = f->scale[1] ? f->scale[1] : 1.0;
            mat3_mul_vec(m, f->uaxis, u2);
            mat3_mul_vec(m, f->vaxis, v2);
            f->shift[0] -= v3_dot(t, u2) / sx;
            f->shift[1] -= v3_dot(t, v2) / sy;
            v3_copy(u2, f->uaxis);
            v3_copy(v2, f->vaxis);
        }
    }
    brush_build(b);
    for (i = 0; i < b->nfaces; i++)
        if (b->faces[i].valve && b->faces[i].planeok)
            face_fix_axes(&b->faces[i]);
}

int brush_snap(brush_t *b, double grid)
{
    brush_t *backup = brush_copy(b);
    int i, k, j;

    for (i = 0; i < b->nfaces; i++) {
        face_t *f = &b->faces[i];
        face_set_points_from_winding(f);
        for (k = 0; k < 3; k++)
            for (j = 0; j < 3; j++)
                f->pts[k][j] = snap_value(f->pts[k][j], grid);
    }
    if (!brush_build(b)) {
        brush_copy_faces(b, backup);
        brush_free(backup);
        brush_build(b);
        return 0;
    }
    brush_free(backup);
    return 1;
}

/* ------------------------------------------------------------------ */
/* CSG                                                                 */

void brush_split(const brush_t *b, const plane_t *pl, brush_t **front, brush_t **back,
                 const face_t *texsrc, const char *tex, int valve)
{
    plane_t flipped;
    brush_t *f, *k;

    plane_flip(pl, &flipped);
    f = brush_copy(b);
    brush_add_plane_face(f, &flipped, texsrc, tex, valve);
    if (!brush_finalize(f)) {
        brush_free(f);
        f = NULL;
    }
    k = brush_copy(b);
    brush_add_plane_face(k, pl, texsrc, tex, valve);
    if (!brush_finalize(k)) {
        brush_free(k);
        k = NULL;
    }
    if (front)
        *front = f;
    else
        brush_free(f);
    if (back)
        *back = k;
    else
        brush_free(k);
}

int brush_intersects(const brush_t *a, const brush_t *b)
{
    int i, j, k, pass;

    if (!a->valid || !b->valid)
        return 0;
    for (i = 0; i < 3; i++)
        if (a->mins[i] >= b->maxs[i] - 0.01 || a->maxs[i] <= b->mins[i] + 0.01)
            return 0;
    /* separating planes taken from the faces of either brush */
    for (pass = 0; pass < 2; pass++) {
        const brush_t *p = pass ? b : a;
        const brush_t *q = pass ? a : b;
        for (i = 0; i < p->nfaces; i++) {
            const face_t *f = &p->faces[i];
            int separated = 1;
            if (!f->w)
                continue;
            for (j = 0; j < q->nfaces && separated; j++) {
                const winding_t *w = q->faces[j].w;
                if (!w)
                    continue;
                for (k = 0; k < w->numpoints; k++)
                    if (plane_dist(&f->plane, w->p[k]) < -0.01) {
                        separated = 0;
                        break;
                    }
            }
            if (separated)
                return 0;
        }
    }
    return 1;
}

int brush_subtract(const brush_t *a, const brush_t *carver, brush_t ***out, int valve)
{
    brush_t *rest;
    brush_t **list = NULL;
    int n = 0, i;

    *out = NULL;
    if (!brush_intersects(a, carver))
        return -1;
    rest = brush_copy(a);
    for (i = 0; i < carver->nfaces; i++) {
        const face_t *cf = &carver->faces[i];
        brush_t *front = NULL, *back = NULL;

        if (!cf->w)
            continue;
        brush_split(rest, &cf->plane, &front, &back, cf, NULL, valve);
        if (!back) {
            /* rest lies entirely outside this plane: no intersection */
            int j;
            brush_free(front);
            brush_free(rest);
            for (j = 0; j < n; j++)
                brush_free(list[j]);
            free(list);
            return -1;
        }
        if (front) {
            list = xrealloc(list, sizeof(brush_t *) * (size_t)(n + 1));
            list[n++] = front;
        }
        brush_free(rest);
        rest = back;
    }
    brush_free(rest);
    *out = list;
    return n;
}

int brush_hollow(const brush_t *b, double thickness, brush_t ***out, int valve)
{
    brush_t **list = NULL;
    int n = 0, i;

    for (i = 0; i < b->nfaces; i++) {
        const face_t *f = &b->faces[i];
        plane_t inner;
        brush_t *wall;

        if (!f->w)
            continue;
        /* the slab between the face and a plane "thickness" units inside */
        v3_scale(f->plane.normal, -1.0, inner.normal);
        inner.dist = -(f->plane.dist - thickness);
        wall = brush_copy(b);
        brush_add_plane_face(wall, &inner, f, NULL, valve);
        if (!brush_finalize(wall)) {
            brush_free(wall);
            continue;
        }
        list = xrealloc(list, sizeof(brush_t *) * (size_t)(n + 1));
        list[n++] = wall;
    }
    *out = list;
    return n;
}

/* ------------------------------------------------------------------ */
/* ray tests                                                           */

int brush_ray(const brush_t *b, const vec3_t org, const vec3_t dir, double *tout, int *faceout)
{
    double tnear = -1e30, tfar = 1e30;
    int fnear = -1, i;

    if (!b->valid)
        return 0;
    for (i = 0; i < b->nfaces; i++) {
        const face_t *f = &b->faces[i];
        double denom, dist, t;
        if (!f->w)
            continue;
        denom = v3_dot(f->plane.normal, dir);
        dist = plane_dist(&f->plane, org);
        if (fabs(denom) < 1e-12) {
            if (dist > 0)
                return 0;
            continue;
        }
        t = -dist / denom;
        if (denom < 0) {
            if (t > tnear) {
                tnear = t;
                fnear = i;
            }
        } else if (t < tfar) {
            tfar = t;
        }
    }
    if (fnear < 0 || tnear > tfar || tnear < 0)
        return 0;
    *tout = tnear;
    if (faceout)
        *faceout = fnear;
    return 1;
}

int bbox_ray(const vec3_t mins, const vec3_t maxs, const vec3_t org, const vec3_t dir, double *tout)
{
    double tmin = -1e30, tmax = 1e30;
    int i;

    for (i = 0; i < 3; i++) {
        if (fabs(dir[i]) < 1e-12) {
            if (org[i] < mins[i] || org[i] > maxs[i])
                return 0;
        } else {
            double t1 = (mins[i] - org[i]) / dir[i];
            double t2 = (maxs[i] - org[i]) / dir[i];
            if (t1 > t2) {
                double t = t1;
                t1 = t2;
                t2 = t;
            }
            if (t1 > tmin)
                tmin = t1;
            if (t2 < tmax)
                tmax = t2;
        }
    }
    if (tmin > tmax || tmin < 0)
        return 0;
    *tout = tmin;
    return 1;
}
