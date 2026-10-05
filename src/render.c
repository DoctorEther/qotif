/*
 * render.c - OpenGL 1.x fixed-function rendering of views and textures
 *
 * Only immediate mode, display-list fonts (glXUseXFont) and plain 2D
 * textures are used, so it runs on any OpenGL 1.1 implementation.
 */
#include "render.h"
#include "eclass.h"
#include "game.h"

#include <GL/gl.h>

static unsigned int font_base;
static int font_w = 7, font_h = 13;
static GLuint missing_tex;

/* ------------------------------------------------------------------ */
/* setup                                                               */

static int next_pow2(int v)
{
    int p = 1;
    while (p < v && p < 1024)
        p <<= 1;
    return p;
}

static GLuint upload_rgba(const unsigned char *rgba, int w, int h)
{
    int pw = next_pow2(w), ph = next_pow2(h), level = 0, x, y, c;
    unsigned char *buf = xmalloc((size_t)pw * ph * 4);
    GLuint id;

    /* nearest resample to a power of two, as OpenGL 1.1 requires */
    for (y = 0; y < ph; y++)
        for (x = 0; x < pw; x++) {
            int sx = x * w / pw, sy = y * h / ph;
            memcpy(buf + (y * pw + x) * 4, rgba + (sy * w + sx) * 4, 4);
        }

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, prefs.tex_linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    prefs.tex_linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pw, ph, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);

    /* box filtered mipmaps */
    while (pw > 1 || ph > 1) {
        int nw = pw > 1 ? pw / 2 : 1, nh = ph > 1 ? ph / 2 : 1;
        unsigned char *mip = xmalloc((size_t)nw * nh * 4);
        for (y = 0; y < nh; y++)
            for (x = 0; x < nw; x++)
                for (c = 0; c < 4; c++) {
                    int x0 = x * pw / nw, y0 = y * ph / nh;
                    int x1 = pw > 1 ? x0 + 1 : x0, y1 = ph > 1 ? y0 + 1 : y0;
                    int sum = buf[(y0 * pw + x0) * 4 + c] + buf[(y0 * pw + x1) * 4 + c]
                            + buf[(y1 * pw + x0) * 4 + c] + buf[(y1 * pw + x1) * 4 + c];
                    mip[(y * nw + x) * 4 + c] = (unsigned char)(sum / 4);
                }
        free(buf);
        buf = mip;
        pw = nw;
        ph = nh;
        glTexImage2D(GL_TEXTURE_2D, ++level, GL_RGBA, pw, ph, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }
    free(buf);
    return id;
}

void render_init(void)
{
    unsigned char checker[16 * 16 * 4];
    int x, y;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            unsigned char *p = checker + (y * 16 + x) * 4;
            int on = ((x / 8) ^ (y / 8)) & 1;
            p[0] = on ? 255 : 40;
            p[1] = 0;
            p[2] = on ? 255 : 40;
            p[3] = 255;
        }
    missing_tex = upload_rgba(checker, 16, 16);
}

void render_set_font(unsigned int list_base, int char_w, int char_h)
{
    font_base = list_base;
    font_w = char_w > 0 ? char_w : 7;
    font_h = char_h > 0 ? char_h : 13;
}

unsigned int render_texture_id(texture_t *t)
{
    if (!t)
        return missing_tex;
    if (!t->glid)
        t->glid = upload_rgba(t->rgba, t->width, t->height);
    return t->glid;
}

void render_texture_free(texture_t *t)
{
    if (t->glid) {
        GLuint id = t->glid;
        glDeleteTextures(1, &id);
        t->glid = 0;
    }
}

void render_reset_textures(void)
{
    int i;
    for (i = 0; i < tex_count(); i++)
        render_texture_free(tex_get(i));
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static void draw_text(double x, double y, const char *s)
{
    if (!font_base || !s || !*s)
        return;
    if (x < 0)
        x = 0;
    if (y < font_h)
        y = font_h;
    glRasterPos2d(x, y);
    glListBase(font_base - 32);
    glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
}

static void pixel_ortho(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
}

static void draw_label(const view_t *v)
{
    const char *label = view_label(v);
    int w = (int)strlen(label) * font_w + 8;

    pixel_ortho(v->width, v->height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
    glRecti(0, 0, w, font_h + 5);
    glDisable(GL_BLEND);
    glColor3fv(prefs.col_text);
    draw_text(4, font_h + 1, label);
    if (active_view == v) {
        glColor3fv(prefs.col_sel);
        glBegin(GL_LINE_LOOP);
        glVertex2f(0.5f, 0.5f);
        glVertex2f(v->width - 0.5f, 0.5f);
        glVertex2f(v->width - 0.5f, v->height - 0.5f);
        glVertex2f(0.5f, v->height - 0.5f);
        glEnd();
    }
}

static void ent_center(const entity_t *e, vec3_t c)
{
    vec3_t mins, maxs;
    ent_bounds(e, mins, maxs);
    v3_add(mins, maxs, c);
    v3_scale(c, 0.5, c);
}

static void brush_color(const brush_t *b, float out[3])
{
    if (b->owner && b->owner->ec && !ent_is_world(b->owner))
        ent_color(b->owner, out);
    else
        memcpy(out, prefs.col_brush, sizeof(float) * 3);
}

/* lines from entities with "target"/"killtarget" to their targets */
static void draw_links(void)
{
    entity_t *e, *o;

    if (!prefs.show_links)
        return;
    glColor3fv(prefs.col_links);
    glBegin(GL_LINES);
    for (e = ed.map->entities; e; e = e->next) {
        const char *keys[2] = { "target", "killtarget" };
        int k;
        for (k = 0; k < 2; k++) {
            const char *t = ent_get(e, keys[k]);
            vec3_t a, b;
            if (!t || !*t)
                continue;
            ent_center(e, a);
            for (o = ed.map->entities; o; o = o->next) {
                const char *tn = ent_get(o, "targetname");
                if (o == e || !tn || strcmp(tn, t))
                    continue;
                ent_center(o, b);
                glVertex3dv(a);
                glVertex3dv(b);
            }
        }
    }
    glEnd();
}

static void draw_pointfile(void)
{
    int i;
    if (!ed.npointfile)
        return;
    glLineWidth(2.0f);
    glColor3fv(prefs.col_pointfile);
    glBegin(GL_LINE_STRIP);
    for (i = 0; i < ed.npointfile; i++)
        glVertex3dv(ed.pointfile[i]);
    glEnd();
    glLineWidth(1.0f);
}

static void box_lines(const vec3_t mins, const vec3_t maxs)
{
    int i;
    glBegin(GL_LINES);
    for (i = 0; i < 12; i++) {
        /* 4 edges along each axis */
        int axis = i / 4, a = (axis + 1) % 3, b = (axis + 2) % 3;
        vec3_t p0, p1;
        p0[a] = (i & 1) ? maxs[a] : mins[a];
        p0[b] = (i & 2) ? maxs[b] : mins[b];
        p0[axis] = mins[axis];
        v3_copy(p0, p1);
        p1[axis] = maxs[axis];
        glVertex3dv(p0);
        glVertex3dv(p1);
    }
    glEnd();
}

/* vertex tool handles, in world coordinates: vertices as big squares,
 * edge midpoints and face centers smaller; selected ones highlighted */
static void draw_vertex_handles(void)
{
    static const float colors[3][3] = {
        { 1.0f, 1.0f, 1.0f },    /* vertex */
        { 0.4f, 0.8f, 1.0f },    /* edge */
        { 1.0f, 0.6f, 0.2f }     /* face */
    };
    static const float sizes[3] = { 7.0f, 5.0f, 5.0f };
    vhandle_t *hs;
    vec3_t *vs;
    int n, i, kind;

    if (ed.tool != TOOL_VERTEX)
        return;
    n = vtx_collect(&hs, &vs);
    for (kind = VH_FACE; kind >= VH_VERTEX; kind--) {
        glPointSize(sizes[kind]);
        glBegin(GL_POINTS);
        for (i = 0; i < n; i++) {
            if (hs[i].kind != kind)
                continue;
            glColor3fv(vtx_handle_selected(&hs[i], vs) ? prefs.col_sel : colors[kind]);
            glVertex3dv(hs[i].pos);
        }
        glEnd();
    }
    glPointSize(1.0f);
    free(hs);
    free(vs);
}

/* the pieces that the clip tool would keep, for its preview */
static void draw_clip_preview(int wire2d, const view_t *v)
{
    plane_t pl;
    entity_t *e;
    brush_t *b;
    int i;

    if (ed.tool != TOOL_CLIP || !clip_plane(&pl))
        return;
    glColor3fv(prefs.col_clip);
    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next) {
            brush_t *pieces[2] = { NULL, NULL };
            int p;
            if (!b->selected)
                continue;
            brush_split(b, &pl, &pieces[0], &pieces[1], NULL, "", 0);
            if (ed.clip_mode == CLIP_BACK) {
                brush_free(pieces[0]);
                pieces[0] = NULL;
            }
            if (ed.clip_mode == CLIP_FRONT) {
                brush_free(pieces[1]);
                pieces[1] = NULL;
            }
            for (p = 0; p < 2; p++) {
                if (!pieces[p])
                    continue;
                for (i = 0; i < pieces[p]->nfaces; i++) {
                    const winding_t *w = pieces[p]->faces[i].w;
                    int k;
                    if (!w)
                        continue;
                    if (wire2d && fabs(pieces[p]->faces[i].plane.normal[v->dax]) < 1e-6)
                        continue;
                    glBegin(GL_LINE_LOOP);
                    for (k = 0; k < w->numpoints; k++)
                        glVertex3dv(w->p[k]);
                    glEnd();
                }
                brush_free(pieces[p]);
            }
        }
}

/* ------------------------------------------------------------------ */
/* 2D views                                                            */

static void vtx2(const view_t *v, const double *p)
{
    glVertex2d(p[v->hax], p[v->vax]);
}

static void draw_grid(const view_t *v, double left, double right, double bottom, double top)
{
    long long step = ed.grid, major = prefs.grid_major;
    double x, y;

    while (step * v->zoom < 4.0)
        step *= 2;
    glBegin(GL_LINES);
    for (x = floor(left / step) * step; x <= right; x += (double)step) {
        long long ix = (long long)floor(x + 0.5);
        if (ix == 0)
            glColor3fv(prefs.col_grid_axis);
        else if (ix % major == 0)
            glColor3fv(prefs.col_grid_major);
        else
            glColor3fv(prefs.col_grid);
        glVertex2d(x, bottom);
        glVertex2d(x, top);
    }
    for (y = floor(bottom / step) * step; y <= top; y += (double)step) {
        long long iy = (long long)floor(y + 0.5);
        if (iy == 0)
            glColor3fv(prefs.col_grid_axis);
        else if (iy % major == 0)
            glColor3fv(prefs.col_grid_major);
        else
            glColor3fv(prefs.col_grid);
        glVertex2d(left, y);
        glVertex2d(right, y);
    }
    glEnd();
}

static void brush_wire_2d(const view_t *v, const brush_t *b)
{
    int i, k;
    for (i = 0; i < b->nfaces; i++) {
        const face_t *f = &b->faces[i];
        if (!f->w || fabs(f->plane.normal[v->dax]) < 1e-6)
            continue;
        glBegin(GL_LINE_LOOP);
        for (k = 0; k < f->w->numpoints; k++)
            vtx2(v, f->w->p[k]);
        glEnd();
    }
}

static void draw_brushes_2d(const view_t *v, int selected)
{
    entity_t *e;
    brush_t *b;
    float c[3];

    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next) {
            if (b->selected != selected || !b->valid)
                continue;
            if (selected)
                glColor3fv(prefs.col_sel);
            else {
                brush_color(b, c);
                glColor3fv(c);
            }
            brush_wire_2d(v, b);
        }
}

static void draw_point_entities_2d(const view_t *v)
{
    entity_t *e;
    float c[3];

    for (e = ed.map->entities; e; e = e->next) {
        vec3_t mins, maxs;
        if (!ent_is_point(e))
            continue;
        ent_bounds(e, mins, maxs);
        ent_color(e, c);
        glColor3f(c[0] * 0.35f, c[1] * 0.35f, c[2] * 0.35f);
        glRectd(mins[v->hax], mins[v->vax], maxs[v->hax], maxs[v->vax]);
        glColor3fv(e->selected ? prefs.col_sel : c);
        glBegin(GL_LINE_LOOP);
        glVertex2d(mins[v->hax], mins[v->vax]);
        glVertex2d(maxs[v->hax], mins[v->vax]);
        glVertex2d(maxs[v->hax], maxs[v->vax]);
        glVertex2d(mins[v->hax], maxs[v->vax]);
        glEnd();
        if (v->type == VIEW_TOP) {
            const char *a = ent_get(e, "angle");
            if (a && atof(a) >= 0) {
                double ang = DEG2RAD(atof(a));
                double r = (maxs[0] - mins[0]) * 0.75;
                glBegin(GL_LINES);
                glVertex2d(e->origin[0], e->origin[1]);
                glVertex2d(e->origin[0] + cos(ang) * r, e->origin[1] + sin(ang) * r);
                glEnd();
            }
        }
    }
}

static void draw_names_2d(const view_t *v)
{
    entity_t *e;

    if (!prefs.show_names || v->zoom < 0.4)
        return;
    for (e = ed.map->entities; e; e = e->next) {
        vec3_t mins, maxs;
        double x, y;
        if (e == ed.map->world)
            continue;
        ent_bounds(e, mins, maxs);
        view_to_screen(v, mins[v->hax], maxs[v->vax], &x, &y);
        if (x < -200 || y < 0 || x > v->width || y > v->height + font_h)
            continue;
        glColor3fv(e->selected ? prefs.col_sel : prefs.col_text);
        draw_text(x, y - 3, ent_classname(e));
    }
}

static void draw_selection_2d(const view_t *v)
{
    vec3_t mins, maxs;
    double x0, y0, x1, y1;
    char buf[32];
    int i, j;

    if (!sel_bounds(mins, maxs) || tr_active())
        return;
    view_to_screen(v, mins[v->hax], mins[v->vax], &x0, &y1);
    view_to_screen(v, maxs[v->hax], maxs[v->vax], &x1, &y0);

    /* dotted bounding box */
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1, 0xAAAA);
    glColor3fv(prefs.col_sel);
    glBegin(GL_LINE_LOOP);
    glVertex2d(x0, y0);
    glVertex2d(x1, y0);
    glVertex2d(x1, y1);
    glVertex2d(x0, y1);
    glEnd();
    glDisable(GL_LINE_STIPPLE);

    /* dimensions */
    glColor3fv(prefs.col_text);
    fmt_num(buf, sizeof(buf), maxs[v->hax] - mins[v->hax]);
    draw_text((x0 + x1) * 0.5 - strlen(buf) * font_w * 0.5, y0 - 10, buf);
    fmt_num(buf, sizeof(buf), maxs[v->vax] - mins[v->vax]);
    draw_text(x0 - 12 - strlen(buf) * font_w, (y0 + y1) * 0.5 + font_h * 0.5, buf);

    if (ed.tool != TOOL_SELECT && ed.tool != TOOL_TEXTURE)
        return;
    for (i = -1; i <= 1; i++)
        for (j = -1; j <= 1; j++) {
            double sx, sy;
            if (!view_handle_pos(v, i, j, mins, maxs, &sx, &sy))
                continue;
            if (ed.handles == HANDLE_SCALE) {
                glColor3f(1, 1, 1);
                glRectd(sx - 3, sy - 3, sx + 3, sy + 3);
                glColor3f(0, 0, 0);
                glBegin(GL_LINE_LOOP);
                glVertex2d(sx - 3.5, sy - 3.5);
                glVertex2d(sx + 3.5, sy - 3.5);
                glVertex2d(sx + 3.5, sy + 3.5);
                glVertex2d(sx - 3.5, sy + 3.5);
                glEnd();
            } else {
                int k;
                glColor3f(1, 1, 1);
                glBegin(GL_POLYGON);
                for (k = 0; k < 12; k++)
                    glVertex2d(sx + cos(k * Q_PI / 6) * 4, sy + sin(k * Q_PI / 6) * 4);
                glEnd();
                glColor3f(0, 0, 0);
                glBegin(GL_LINE_LOOP);
                for (k = 0; k < 12; k++)
                    glVertex2d(sx + cos(k * Q_PI / 6) * 4.5, sy + sin(k * Q_PI / 6) * 4.5);
                glEnd();
            }
        }
}

static void draw_camera_2d(const view_t *v)
{
    vec3_t fwd, right, up;
    double x, y, dx, dy, len;
    int i;

    camera_vectors(&ed.cam, fwd, right, up);
    view_to_screen(v, ed.cam.pos[v->hax], ed.cam.pos[v->vax], &x, &y);
    dx = fwd[v->hax];
    dy = -fwd[v->vax];
    len = sqrt(dx * dx + dy * dy);
    glColor3fv(prefs.col_camera);
    glRectd(x - 3, y - 3, x + 3, y + 3);
    if (len < 1e-3)
        return;
    dx /= len;
    dy /= len;
    glBegin(GL_LINES);
    for (i = -1; i <= 1; i += 2) {
        /* field of view wedge */
        double a = DEG2RAD(prefs.fov * 0.5) * i;
        double rx = dx * cos(a) - dy * sin(a), ry = dx * sin(a) + dy * cos(a);
        glVertex2d(x, y);
        glVertex2d(x + rx * 40, y + ry * 40);
    }
    glVertex2d(x, y);
    glVertex2d(x + dx * 48, y + dy * 48);
    glEnd();
}

static void draw_clip_2d(const view_t *v)
{
    int i;
    double x0, y0, x1, y1;

    if (ed.tool != TOOL_CLIP || !ed.clip_npts || ed.clip_axis != v->dax)
        return;
    glColor3fv(prefs.col_clip);
    for (i = 0; i < ed.clip_npts; i++) {
        view_to_screen(v, ed.clip_pts[i][v->hax], ed.clip_pts[i][v->vax], &x0, &y0);
        glRectd(x0 - 3, y0 - 3, x0 + 3, y0 + 3);
    }
    if (ed.clip_npts < 2)
        return;
    view_to_screen(v, ed.clip_pts[0][v->hax], ed.clip_pts[0][v->vax], &x0, &y0);
    view_to_screen(v, ed.clip_pts[1][v->hax], ed.clip_pts[1][v->vax], &x1, &y1);
    {
        double dx = x1 - x0, dy = y1 - y0, l = sqrt(dx * dx + dy * dy);
        if (l < 1e-6)
            return;
        dx /= l;
        dy /= l;
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(2, 0xF0F0);
        glBegin(GL_LINES);
        glVertex2d(x0 - dx * 10000, y0 - dy * 10000);
        glVertex2d(x0 + dx * 10000, y0 + dy * 10000);
        glEnd();
        glDisable(GL_LINE_STIPPLE);
    }
}

static void render_2d(view_t *v)
{
    double hw = v->width * 0.5 / v->zoom, hh = v->height * 0.5 / v->zoom;
    double left = v->cx - hw, right = v->cx + hw, bottom = v->cy - hh, top = v->cy + hh;

    glClearColor(prefs.col_bg2d[0], prefs.col_bg2d[1], prefs.col_bg2d[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(left, right, bottom, top, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (ed.show_grid)
        draw_grid(v, left, right, bottom, top);

    draw_point_entities_2d(v);
    draw_brushes_2d(v, 0);
    draw_brushes_2d(v, 1);

    /* 3D helpers drawn through a matrix that drops the depth axis */
    {
        GLdouble m[16];
        int i;
        for (i = 0; i < 16; i++)
            m[i] = 0.0;
        m[v->hax * 4 + 0] = 1.0;   /* column major: world axis -> screen x */
        m[v->vax * 4 + 1] = 1.0;   /* world axis -> screen y */
        m[15] = 1.0;
        glPushMatrix();
        glMultMatrixd(m);
        draw_links();
        draw_pointfile();
        draw_clip_preview(1, v);
        draw_vertex_handles();
        if (ed.preview) {
            glColor3fv(prefs.col_clip);
            box_lines(ed.preview_mins, ed.preview_maxs);
        }
        glPopMatrix();
    }

    pixel_ortho(v->width, v->height);
    draw_names_2d(v);
    draw_selection_2d(v);
    draw_camera_2d(v);
    draw_clip_2d(v);
    if (v->mode == MODE_RUBBER && v->moved) {
        glColor3f(1, 1, 1);
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(1, 0xCCCC);
        glBegin(GL_LINE_LOOP);
        glVertex2i(v->press_x, v->press_y);
        glVertex2i(v->mouse_x, v->press_y);
        glVertex2i(v->mouse_x, v->mouse_y);
        glVertex2i(v->press_x, v->mouse_y);
        glEnd();
        glDisable(GL_LINE_STIPPLE);
    }
    draw_label(v);
}

/* ------------------------------------------------------------------ */
/* 3D view                                                             */

static float face_shade(const double *n)
{
    /* fixed directional light so faces stay distinguishable */
    double d = n[0] * 0.3 + n[1] * 0.5 + n[2] * 0.81;
    return (float)(0.62 + 0.38 * (d > 0 ? d : d * 0.4));
}

static void face_polygon(const face_t *f, texture_t *tex, int textured)
{
    const winding_t *w = f->w;
    double vecs[2][4];
    double tw = tex ? tex->width : 64, th = tex ? tex->height : 64;
    int k;

    if (textured)
        face_texture_vecs(f, vecs);
    glBegin(GL_POLYGON);
    for (k = 0; k < w->numpoints; k++) {
        if (textured)
            glTexCoord2d((v3_dot(w->p[k], vecs[0]) + vecs[0][3]) / tw,
                         (v3_dot(w->p[k], vecs[1]) + vecs[1][3]) / th);
        glVertex3dv(w->p[k]);
    }
    glEnd();
}

static void draw_brush_faces_3d(const view_t *v, const brush_t *b)
{
    int i;
    float ec[3];
    int world = !b->owner || ent_is_world(b->owner);

    if (!world)
        ent_color(b->owner, ec);
    for (i = 0; i < b->nfaces; i++) {
        const face_t *f = &b->faces[i];
        texture_t *tex;
        float s;

        if (!f->w)
            continue;
        tex = tex_find(f->tex);
        s = face_shade(f->plane.normal);
        if (v->rmode == RENDER_TEXTURED) {
            glBindTexture(GL_TEXTURE_2D, render_texture_id(tex));
            glColor3f(s, s, s);
            face_polygon(f, tex, 1);
        } else {
            float c[3] = { 0.55f, 0.55f, 0.55f };
            if (tex)
                memcpy(c, tex->avg, sizeof(c));
            if (!world) {
                c[0] = c[0] * 0.5f + ec[0] * 0.5f;
                c[1] = c[1] * 0.5f + ec[1] * 0.5f;
                c[2] = c[2] * 0.5f + ec[2] * 0.5f;
            }
            glColor3f(c[0] * s, c[1] * s, c[2] * s);
            face_polygon(f, NULL, 0);
        }
    }
}

static void brush_edges_3d(const brush_t *b)
{
    int i, k;
    for (i = 0; i < b->nfaces; i++) {
        const winding_t *w = b->faces[i].w;
        if (!w)
            continue;
        glBegin(GL_LINE_LOOP);
        for (k = 0; k < w->numpoints; k++)
            glVertex3dv(w->p[k]);
        glEnd();
    }
}

static void solid_box(const vec3_t mins, const vec3_t maxs, const float *c, int shaded)
{
    static const int faces[6][4] = {
        { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
        { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 }
    };
    static const double normals[6][3] = {
        { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }
    };
    vec3_t p[8];
    int i, k;

    for (i = 0; i < 8; i++)
        v3_set(p[i], (i & 4) ? maxs[0] : mins[0], (i & 2) ? maxs[1] : mins[1], (i & 1) ? maxs[2] : mins[2]);
    glBegin(GL_QUADS);
    for (i = 0; i < 6; i++) {
        float s = shaded ? face_shade(normals[i]) : 1.0f;
        if (c)
            glColor3f(c[0] * s, c[1] * s, c[2] * s);
        for (k = 0; k < 4; k++)
            glVertex3dv(p[faces[i][k]]);
    }
    glEnd();
}

static void setup_camera(const view_t *v)
{
    double aspect = v->height > 0 ? (double)v->width / v->height : 1.0;
    double znear = 2.0, top = znear * tan(DEG2RAD(prefs.fov) * 0.5);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-top * aspect, top * aspect, -top, top, znear, 65536.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotated(-90, 1, 0, 0);    /* Quake: z up, looking down +x */
    glRotated(90, 0, 0, 1);
    glRotated(ed.cam.pitch, 0, 1, 0);
    glRotated(-ed.cam.yaw, 0, 0, 1);
    glTranslated(-ed.cam.pos[0], -ed.cam.pos[1], -ed.cam.pos[2]);
}

static void render_3d(view_t *v)
{
    entity_t *e;
    brush_t *b;
    int i;

    glClearColor(prefs.col_bg3d[0], prefs.col_bg3d[1], prefs.col_bg3d[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    setup_camera(v);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    if (v->rmode == RENDER_WIRE) {
        float c[3];
        glDisable(GL_TEXTURE_2D);
        for (e = ed.map->entities; e; e = e->next)
            for (b = e->brushes; b; b = b->next) {
                if (!b->valid)
                    continue;
                if (b->selected)
                    glColor3fv(prefs.col_sel);
                else {
                    brush_color(b, c);
                    glColor3fv(c);
                }
                brush_edges_3d(b);
            }
    } else {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        if (v->rmode == RENDER_TEXTURED) {
            glEnable(GL_TEXTURE_2D);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.5f);
        }
        for (e = ed.map->entities; e; e = e->next)
            for (b = e->brushes; b; b = b->next)
                if (b->valid)
                    draw_brush_faces_3d(v, b);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_POLYGON_OFFSET_FILL);

        /* selection tint */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (e = ed.map->entities; e; e = e->next)
            for (b = e->brushes; b; b = b->next) {
                if (!b->valid)
                    continue;
                for (i = 0; i < b->nfaces; i++) {
                    const face_t *f = &b->faces[i];
                    if (!f->w)
                        continue;
                    if (f->selected)
                        glColor4f(prefs.col_clip[0], prefs.col_clip[1], prefs.col_clip[2], 0.4f);
                    else if (b->selected)
                        glColor4f(prefs.col_sel[0], prefs.col_sel[1], prefs.col_sel[2], 0.3f);
                    else
                        continue;
                    glPolygonOffset(-1.0f, -1.0f);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    face_polygon(f, NULL, 0);
                    glDisable(GL_POLYGON_OFFSET_FILL);
                }
            }
        glDepthMask(GL_TRUE);

        /* hidden selected edges dimmed, visible ones bright */
        for (i = 0; i < 2; i++) {
            if (i == 0) {
                glDisable(GL_DEPTH_TEST);
                glColor4f(prefs.col_sel[0], prefs.col_sel[1], prefs.col_sel[2], 0.35f);
            } else {
                glEnable(GL_DEPTH_TEST);
                glColor4f(prefs.col_sel[0], prefs.col_sel[1], prefs.col_sel[2], 1.0f);
            }
            for (e = ed.map->entities; e; e = e->next)
                for (b = e->brushes; b; b = b->next)
                    if (b->valid && b->selected)
                        brush_edges_3d(b);
        }
        glDisable(GL_BLEND);
    }

    /* point entities */
    for (e = ed.map->entities; e; e = e->next) {
        vec3_t mins, maxs;
        float c[3];
        if (!ent_is_point(e))
            continue;
        ent_bounds(e, mins, maxs);
        ent_color(e, c);
        if (v->rmode != RENDER_WIRE) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
            solid_box(mins, maxs, c, 1);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        glColor3fv(e->selected ? prefs.col_sel : c);
        box_lines(mins, maxs);
    }

    draw_links();
    draw_pointfile();
    draw_clip_preview(0, v);
    glDisable(GL_DEPTH_TEST);
    draw_vertex_handles();
    if (ed.preview) {
        glColor3fv(prefs.col_clip);
        box_lines(ed.preview_mins, ed.preview_maxs);
    }

    pixel_ortho(v->width, v->height);
    /* crosshair */
    glColor3f(1, 1, 1);
    glBegin(GL_LINES);
    glVertex2i(v->width / 2 - 5, v->height / 2);
    glVertex2i(v->width / 2 + 6, v->height / 2);
    glVertex2i(v->width / 2, v->height / 2 - 5);
    glVertex2i(v->width / 2, v->height / 2 + 6);
    glEnd();
    draw_label(v);
}

void render_view(view_t *v)
{
    if (v->width <= 0 || v->height <= 0)
        return;
    glViewport(0, 0, v->width, v->height);
    if (v->type == VIEW_3D)
        render_3d(v);
    else
        render_2d(v);
}

/* ------------------------------------------------------------------ */
/* texture browser                                                     */

void texbrowser_update_used(void)
{
    entity_t *e;
    brush_t *b;
    int i;

    for (i = 0; i < tex_count(); i++)
        tex_get(i)->used = 0;
    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next)
            for (i = 0; i < b->nfaces; i++) {
                texture_t *t = tex_find(b->faces[i].tex);
                if (t)
                    t->used = 1;
            }
}

static int tb_visible(const texbrowser_t *tb, const texture_t *t)
{
    if (tb->used_only && !t->used)
        return 0;
    return !tb->filter[0] || str_icontains(t->name, tb->filter);
}

typedef struct cell_s {
    int x, y, w, h;
} cell_t;

static void tb_layout(texbrowser_t *tb, int k, cell_t *c)
{
    int cell_w = tb->thumb + 16;
    int row_h = tb->thumb + font_h + 14;
    int cols = (tb->width - 8) / cell_w;
    if (cols < 1)
        cols = 1;
    c->x = 8 + (k % cols) * cell_w;
    c->y = 8 + (k / cols) * row_h - tb->scroll;
    c->w = cell_w;
    c->h = row_h;
}

void texbrowser_render(texbrowser_t *tb)
{
    int i, k = 0;
    cell_t c;

    glViewport(0, 0, tb->width, tb->height);
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    pixel_ortho(tb->width, tb->height);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    for (i = 0; i < tex_count(); i++) {
        texture_t *t = tex_get(i);
        double s, w, h;
        char name[40];
        int maxchars;

        if (!tb_visible(tb, t))
            continue;
        tb_layout(tb, k++, &c);
        if (c.y + c.h < 0 || c.y > tb->height)
            continue;
        s = (double)tb->thumb / (t->width > t->height ? t->width : t->height);
        if (s > 1.0)
            s = 1.0;
        w = t->width * s;
        h = t->height * s;

        if (str_ieq(t->name, ed.texture)) {
            glColor3fv(prefs.col_sel);
            glRecti(c.x - 4, c.y - 4, c.x + tb->thumb + 4, c.y + tb->thumb + font_h + 8);
            glColor3f(0.12f, 0.12f, 0.12f);
            glRecti(c.x - 2, c.y - 2, c.x + tb->thumb + 2, c.y + tb->thumb + font_h + 6);
        }
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, render_texture_id(t));
        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2d(c.x, c.y);
        glTexCoord2f(1, 0); glVertex2d(c.x + w, c.y);
        glTexCoord2f(1, 1); glVertex2d(c.x + w, c.y + h);
        glTexCoord2f(0, 1); glVertex2d(c.x, c.y + h);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        maxchars = (tb->thumb + 12) / font_w;
        str_copy(name, t->name, sizeof(name));
        if ((int)strlen(name) > maxchars && maxchars > 0)
            name[maxchars] = 0;
        glColor3f(0.9f, 0.9f, 0.9f);
        draw_text(c.x, c.y + tb->thumb + font_h + 2, name);
    }
    if (k) {
        tb_layout(tb, k - 1, &c);
        tb->content_height = c.y + tb->scroll + c.h + 8;
    } else {
        tb->content_height = 0;
        glColor3f(0.8f, 0.8f, 0.8f);
        draw_text(10, 24, tex_count() ? "No texture matches the filter." :
                  "No textures loaded: set the game path and the worldspawn 'wad' key.");
    }
}

int texbrowser_hit(texbrowser_t *tb, int x, int y)
{
    int i, k = 0;
    cell_t c;

    for (i = 0; i < tex_count(); i++) {
        if (!tb_visible(tb, tex_get(i)))
            continue;
        tb_layout(tb, k++, &c);
        if (x >= c.x - 4 && x < c.x + c.w - 4 && y >= c.y - 4 && y < c.y + c.h - 4)
            return i;
    }
    return -1;
}

int texbrowser_find(texbrowser_t *tb, const char *name)
{
    int i, k = 0;
    cell_t c;

    for (i = 0; i < tex_count(); i++) {
        if (!tb_visible(tb, tex_get(i)))
            continue;
        if (str_ieq(tex_get(i)->name, name)) {
            tb_layout(tb, k, &c);
            return c.y + tb->scroll;
        }
        k++;
    }
    return -1;
}

void render_texture_preview(int w, int h)
{
    texture_t *t = tex_find(ed.texture);

    glViewport(0, 0, w, h);
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    pixel_ortho(w, h);
    if (t) {
        double s = (double)(w - 8) / t->width;
        double s2 = (double)(h - 8) / t->height;
        double tw, th, x, y;
        if (s2 < s)
            s = s2;
        if (s > 2.0)
            s = 2.0;
        tw = t->width * s;
        th = t->height * s;
        x = (w - tw) * 0.5;
        y = (h - th) * 0.5;
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glBindTexture(GL_TEXTURE_2D, render_texture_id(t));
        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2d(x, y);
        glTexCoord2f(1, 0); glVertex2d(x + tw, y);
        glTexCoord2f(1, 1); glVertex2d(x + tw, y + th);
        glTexCoord2f(0, 1); glVertex2d(x, y + th);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    } else {
        glColor3f(0.8f, 0.8f, 0.8f);
        draw_text(8, h / 2, ed.texture[0] ? "(missing)" : "(none)");
    }
}
