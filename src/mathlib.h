/*
 * mathlib.h - vectors, planes, matrices and polygons (windings)
 */
#ifndef QOTIF_MATHLIB_H
#define QOTIF_MATHLIB_H

#include <math.h>

typedef double vec3_t[3];
typedef double mat3_t[3][3];

#define Q_PI 3.14159265358979323846
#define DEG2RAD(a) ((a) * (Q_PI / 180.0))
#define RAD2DEG(a) ((a) * (180.0 / Q_PI))

/* half the size of the "infinite" polygon used to build brush faces */
#define WORLD_HALF 262144.0

static inline void v3_set(vec3_t v, double x, double y, double z)
{
    v[0] = x; v[1] = y; v[2] = z;
}

static inline void v3_clear(vec3_t v)
{
    v[0] = v[1] = v[2] = 0.0;
}

static inline void v3_copy(const vec3_t a, vec3_t out)
{
    out[0] = a[0]; out[1] = a[1]; out[2] = a[2];
}

static inline void v3_add(const vec3_t a, const vec3_t b, vec3_t out)
{
    out[0] = a[0] + b[0]; out[1] = a[1] + b[1]; out[2] = a[2] + b[2];
}

static inline void v3_sub(const vec3_t a, const vec3_t b, vec3_t out)
{
    out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
}

static inline void v3_scale(const vec3_t a, double s, vec3_t out)
{
    out[0] = a[0] * s; out[1] = a[1] * s; out[2] = a[2] * s;
}

/* out = a + s * b */
static inline void v3_ma(const vec3_t a, double s, const vec3_t b, vec3_t out)
{
    out[0] = a[0] + s * b[0];
    out[1] = a[1] + s * b[1];
    out[2] = a[2] + s * b[2];
}

static inline double v3_dot(const vec3_t a, const vec3_t b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void v3_cross(const vec3_t a, const vec3_t b, vec3_t out)
{
    double x = a[1] * b[2] - a[2] * b[1];
    double y = a[2] * b[0] - a[0] * b[2];
    double z = a[0] * b[1] - a[1] * b[0];
    out[0] = x; out[1] = y; out[2] = z;
}

static inline double v3_len(const vec3_t a)
{
    return sqrt(v3_dot(a, a));
}

static inline double v3_normalize(vec3_t v)
{
    double l = v3_len(v);
    if (l > 0.0) {
        v[0] /= l; v[1] /= l; v[2] /= l;
    }
    return l;
}

static inline int v3_equal(const vec3_t a, const vec3_t b, double eps)
{
    return fabs(a[0] - b[0]) <= eps && fabs(a[1] - b[1]) <= eps && fabs(a[2] - b[2]) <= eps;
}

typedef struct plane_s {
    vec3_t normal;
    double dist;
} plane_t;

/* Quake convention: normal = (p0 - p1) x (p2 - p1), pointing out of the brush */
int plane_from_points(plane_t *pl, const vec3_t p0, const vec3_t p1, const vec3_t p2);
int plane_equal(const plane_t *a, const plane_t *b);
void plane_flip(const plane_t *in, plane_t *out);

static inline double plane_dist(const plane_t *pl, const vec3_t p)
{
    return v3_dot(pl->normal, p) - pl->dist;
}

void bounds_clear(vec3_t mins, vec3_t maxs);
void bounds_add(vec3_t mins, vec3_t maxs, const vec3_t p);
int bounds_valid(const vec3_t mins, const vec3_t maxs);

double snap_value(double v, double grid);
double round_if_near(double v, double eps);

void mat3_identity(mat3_t m);
void mat3_scale(mat3_t m, double sx, double sy, double sz);
void mat3_rotation(mat3_t m, int axis, double degrees);
void mat3_mul_vec(mat3_t m, const vec3_t v, vec3_t out);
double mat3_det(mat3_t m);
int mat3_is_identity(mat3_t m);
int mat3_is_orthonormal(mat3_t m);

/* convex polygon */
typedef struct winding_s {
    int numpoints;
    vec3_t p[];
} winding_t;

winding_t *winding_alloc(int n);
winding_t *winding_copy(const winding_t *w);
void winding_free(winding_t *w);
winding_t *winding_base(const plane_t *pl);
winding_t *winding_clip(winding_t *in, const plane_t *pl, double eps);
void winding_center(const winding_t *w, vec3_t c);
double winding_area(const winding_t *w);

#endif
