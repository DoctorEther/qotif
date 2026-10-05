/*
 * mathlib.c - vectors, planes, matrices and polygons (windings)
 */
#include "mathlib.h"
#include "common.h"

enum { SIDE_FRONT, SIDE_BACK, SIDE_ON };

int plane_from_points(plane_t *pl, const vec3_t p0, const vec3_t p1, const vec3_t p2)
{
    vec3_t t1, t2;
    int i;

    v3_sub(p0, p1, t1);
    v3_sub(p2, p1, t2);
    v3_cross(t1, t2, pl->normal);
    if (v3_normalize(pl->normal) < 1e-9) {
        v3_clear(pl->normal);
        pl->dist = 0.0;
        return 0;
    }
    /* make axial normals exact so axial faces stay exact */
    for (i = 0; i < 3; i++) {
        if (fabs(fabs(pl->normal[i]) - 1.0) < 1e-12) {
            double s = pl->normal[i] > 0 ? 1.0 : -1.0;
            v3_clear(pl->normal);
            pl->normal[i] = s;
            break;
        }
        if (fabs(pl->normal[i]) < 1e-14)
            pl->normal[i] = 0.0;
    }
    pl->dist = v3_dot(p1, pl->normal);
    return 1;
}

int plane_equal(const plane_t *a, const plane_t *b)
{
    return fabs(a->dist - b->dist) < 0.01
        && fabs(a->normal[0] - b->normal[0]) < 1e-6
        && fabs(a->normal[1] - b->normal[1]) < 1e-6
        && fabs(a->normal[2] - b->normal[2]) < 1e-6;
}

void plane_flip(const plane_t *in, plane_t *out)
{
    v3_scale(in->normal, -1.0, out->normal);
    out->dist = -in->dist;
}

void bounds_clear(vec3_t mins, vec3_t maxs)
{
    v3_set(mins, 1e30, 1e30, 1e30);
    v3_set(maxs, -1e30, -1e30, -1e30);
}

void bounds_add(vec3_t mins, vec3_t maxs, const vec3_t p)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (p[i] < mins[i])
            mins[i] = p[i];
        if (p[i] > maxs[i])
            maxs[i] = p[i];
    }
}

int bounds_valid(const vec3_t mins, const vec3_t maxs)
{
    return mins[0] <= maxs[0] && mins[1] <= maxs[1] && mins[2] <= maxs[2];
}

double snap_value(double v, double grid)
{
    if (grid <= 0.0)
        return v;
    return floor(v / grid + 0.5) * grid;
}

double round_if_near(double v, double eps)
{
    double r = floor(v + 0.5);
    return fabs(v - r) < eps ? r : v;
}

void mat3_identity(mat3_t m)
{
    mat3_scale(m, 1.0, 1.0, 1.0);
}

void mat3_scale(mat3_t m, double sx, double sy, double sz)
{
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            m[i][j] = 0.0;
    m[0][0] = sx;
    m[1][1] = sy;
    m[2][2] = sz;
}

void mat3_rotation(mat3_t m, int axis, double degrees)
{
    double a = DEG2RAD(degrees);
    double c = round_if_near(cos(a), 1e-12);
    double s = round_if_near(sin(a), 1e-12);
    int u = (axis + 1) % 3, v = (axis + 2) % 3;

    mat3_identity(m);
    m[u][u] = c;
    m[u][v] = -s;
    m[v][u] = s;
    m[v][v] = c;
}

void mat3_mul_vec(mat3_t m, const vec3_t v, vec3_t out)
{
    double x = m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2];
    double y = m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2];
    double z = m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2];
    out[0] = x; out[1] = y; out[2] = z;
}

double mat3_det(mat3_t m)
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
         - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

int mat3_is_identity(mat3_t m)
{
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            if (fabs(m[i][j] - (i == j ? 1.0 : 0.0)) > 1e-12)
                return 0;
    return 1;
}

int mat3_is_orthonormal(mat3_t m)
{
    int i, j, k;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            double d = 0.0;
            for (k = 0; k < 3; k++)
                d += m[k][i] * m[k][j];
            if (fabs(d - (i == j ? 1.0 : 0.0)) > 1e-9)
                return 0;
        }
    }
    return 1;
}

winding_t *winding_alloc(int n)
{
    winding_t *w = xmalloc(sizeof(winding_t) + sizeof(vec3_t) * (size_t)n);
    w->numpoints = n;
    return w;
}

winding_t *winding_copy(const winding_t *w)
{
    winding_t *c;
    if (!w)
        return NULL;
    c = winding_alloc(w->numpoints);
    memcpy(c->p, w->p, sizeof(vec3_t) * (size_t)w->numpoints);
    return c;
}

void winding_free(winding_t *w)
{
    free(w);
}

winding_t *winding_base(const plane_t *pl)
{
    int i, x = 0;
    double max = -1.0, v;
    vec3_t org, vright, vup;
    winding_t *w;

    for (i = 0; i < 3; i++) {
        v = fabs(pl->normal[i]);
        if (v > max) {
            x = i;
            max = v;
        }
    }
    v3_clear(vup);
    if (x == 2)
        vup[0] = 1.0;
    else
        vup[2] = 1.0;
    v = v3_dot(vup, pl->normal);
    v3_ma(vup, -v, pl->normal, vup);
    v3_normalize(vup);
    v3_scale(pl->normal, pl->dist, org);
    v3_cross(vup, pl->normal, vright);
    v3_scale(vup, WORLD_HALF, vup);
    v3_scale(vright, WORLD_HALF, vright);

    w = winding_alloc(4);
    v3_sub(org, vright, w->p[0]);
    v3_add(w->p[0], vup, w->p[0]);
    v3_add(org, vright, w->p[1]);
    v3_add(w->p[1], vup, w->p[1]);
    v3_add(org, vright, w->p[2]);
    v3_sub(w->p[2], vup, w->p[2]);
    v3_sub(org, vright, w->p[3]);
    v3_sub(w->p[3], vup, w->p[3]);
    return w;
}

/* keeps the part of the polygon behind the plane; frees the input when a
 * new winding is produced */
winding_t *winding_clip(winding_t *in, const plane_t *pl, double eps)
{
    int n = in->numpoints, i, j;
    int counts[3] = { 0, 0, 0 };
    double *dists;
    int *sides;
    winding_t *out;

    dists = xmalloc(sizeof(double) * (size_t)(n + 1));
    sides = xmalloc(sizeof(int) * (size_t)(n + 1));
    for (i = 0; i < n; i++) {
        double d = plane_dist(pl, in->p[i]);
        dists[i] = d;
        sides[i] = d > eps ? SIDE_FRONT : (d < -eps ? SIDE_BACK : SIDE_ON);
        counts[sides[i]]++;
    }
    dists[n] = dists[0];
    sides[n] = sides[0];

    if (!counts[SIDE_FRONT]) {
        free(dists);
        free(sides);
        return in;
    }
    if (!counts[SIDE_BACK]) {
        free(dists);
        free(sides);
        winding_free(in);
        return NULL;
    }

    out = winding_alloc(n + 4);
    out->numpoints = 0;
    for (i = 0; i < n; i++) {
        const double *p1 = in->p[i];
        const double *p2;
        double t;
        vec3_t mid;

        if (sides[i] == SIDE_ON) {
            v3_copy(p1, out->p[out->numpoints++]);
            continue;
        }
        if (sides[i] == SIDE_BACK)
            v3_copy(p1, out->p[out->numpoints++]);
        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
            continue;

        p2 = in->p[(i + 1) % n];
        t = dists[i] / (dists[i] - dists[i + 1]);
        for (j = 0; j < 3; j++) {
            if (pl->normal[j] == 1.0)
                mid[j] = pl->dist;
            else if (pl->normal[j] == -1.0)
                mid[j] = -pl->dist;
            else
                mid[j] = p1[j] + t * (p2[j] - p1[j]);
        }
        v3_copy(mid, out->p[out->numpoints++]);
    }
    free(dists);
    free(sides);
    winding_free(in);
    return out;
}

void winding_center(const winding_t *w, vec3_t c)
{
    int i;
    v3_clear(c);
    for (i = 0; i < w->numpoints; i++)
        v3_add(c, w->p[i], c);
    if (w->numpoints)
        v3_scale(c, 1.0 / w->numpoints, c);
}

double winding_area(const winding_t *w)
{
    int i;
    double total = 0.0;
    vec3_t d1, d2, cr;

    for (i = 2; i < w->numpoints; i++) {
        v3_sub(w->p[i - 1], w->p[0], d1);
        v3_sub(w->p[i], w->p[0], d2);
        v3_cross(d1, d2, cr);
        total += 0.5 * v3_len(cr);
    }
    return total;
}
