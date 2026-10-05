/*
 * brush.h - brush geometry, primitives, transforms, CSG and texturing
 */
#ifndef QOTIF_BRUSH_H
#define QOTIF_BRUSH_H

#include "map.h"

enum { PRIM_BLOCK, PRIM_WEDGE, PRIM_CYLINDER, PRIM_SPIKE, PRIM_COUNT };

/* computes face polygons and bounds; returns 1 if the brush is a valid
 * closed convex solid */
int brush_build(brush_t *b);
/* removes redundant faces and rewrites the plane points from the polygons */
int brush_finalize(brush_t *b);

face_t *brush_add_plane_face(brush_t *b, const plane_t *pl, const face_t *texsrc,
                             const char *tex, int valve);
brush_t *brush_from_bounds(const vec3_t mins, const vec3_t maxs, const char *tex, int valve);
brush_t *brush_from_points(vec3_t *pts, int n, const char *tex, int valve);
brush_t *brush_primitive(int prim, int sides, int hax, int vax, int dax,
                         const vec3_t mins, const vec3_t maxs, const char *tex, int valve);

void brush_translate(brush_t *b, const vec3_t d, int texlock);
void brush_transform(brush_t *b, mat3_t m, const vec3_t t, int texlock);
int brush_snap(brush_t *b, double grid);

/* splits a brush by a plane; front = part where dot(n, p) >= dist.
 * Either result may be NULL. New faces copy texsrc or use tex. */
void brush_split(const brush_t *b, const plane_t *pl, brush_t **front, brush_t **back,
                 const face_t *texsrc, const char *tex, int valve);
/* returns -1 when the brushes do not intersect, otherwise the number of
 * pieces of a left outside the carver (stored in *out) */
int brush_subtract(const brush_t *a, const brush_t *carver, brush_t ***out, int valve);
int brush_hollow(const brush_t *b, double thickness, brush_t ***out, int valve);
int brush_intersects(const brush_t *a, const brush_t *b);

int brush_ray(const brush_t *b, const vec3_t org, const vec3_t dir, double *tout, int *faceout);
int bbox_ray(const vec3_t mins, const vec3_t maxs, const vec3_t org, const vec3_t dir, double *tout);

void face_points_from_plane(face_t *f, const plane_t *pl);
void face_set_points_from_winding(face_t *f);
void face_init_texture(face_t *f, const char *tex, int valve);
void face_fix_axes(face_t *f);
void face_texture_axis(const vec3_t normal, vec3_t xv, vec3_t yv);
/* s = dot(p, vecs[0]) + vecs[0][3], in texels */
void face_texture_vecs(const face_t *f, double vecs[2][4]);
void face_to_valve(face_t *f);
void face_fit_texture(face_t *f, int texw, int texh);

#endif
