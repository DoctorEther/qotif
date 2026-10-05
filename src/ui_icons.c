/*
 * ui_icons.c - tool palette icons drawn with Xlib (no image files needed)
 */
#include "ui_internal.h"

#define ICON_SIZE 24

static void fill_poly(Display *d, Pixmap pm, GC gc, const short *xy, int n)
{
    XPoint pts[16];
    int i;
    for (i = 0; i < n && i < 16; i++) {
        pts[i].x = xy[i * 2];
        pts[i].y = xy[i * 2 + 1];
    }
    XFillPolygon(d, pm, gc, pts, n, Complex, CoordModeOrigin);
}

Pixmap ui_make_icon(Widget w, int icon)
{
    Display *d = XtDisplay(w);
    Pixel bg, fg;
    Cardinal depth;
    Pixmap pm;
    GC gc;
    int x, y;

    XtVaGetValues(w, XmNbackground, &bg, XmNforeground, &fg, XmNdepth, &depth, NULL);
    pm = XCreatePixmap(d, RootWindowOfScreen(XtScreen(w)), ICON_SIZE, ICON_SIZE, depth);
    gc = XCreateGC(d, pm, 0, NULL);
    XSetForeground(d, gc, bg);
    XFillRectangle(d, pm, gc, 0, 0, ICON_SIZE, ICON_SIZE);
    XSetForeground(d, gc, fg);
    XSetLineAttributes(d, gc, 1, LineSolid, CapButt, JoinMiter);

    switch (icon) {
    case ICON_SELECT: {
        static const short arrow[] = { 6, 2, 6, 19, 10, 15, 13, 22, 16, 21, 13, 14, 19, 14 };
        fill_poly(d, pm, gc, arrow, 7);
        break;
    }
    case ICON_CAMERA: {
        static const short lens[] = { 15, 10, 21, 6, 21, 18, 15, 14 };
        XFillRectangle(d, pm, gc, 2, 8, 13, 9);
        fill_poly(d, pm, gc, lens, 4);
        XDrawArc(d, pm, gc, 3, 2, 6, 6, 0, 360 * 64);
        XDrawArc(d, pm, gc, 9, 2, 6, 6, 0, 360 * 64);
        break;
    }
    case ICON_ENTITY:
        /* light bulb */
        XFillArc(d, pm, gc, 6, 2, 12, 12, 0, 360 * 64);
        XFillRectangle(d, pm, gc, 9, 13, 6, 3);
        XDrawLine(d, pm, gc, 9, 17, 14, 17);
        XDrawLine(d, pm, gc, 9, 19, 14, 19);
        XDrawLine(d, pm, gc, 10, 21, 13, 21);
        break;
    case ICON_BLOCK:
        XDrawRectangle(d, pm, gc, 3, 9, 12, 12);
        XDrawLine(d, pm, gc, 3, 9, 9, 3);
        XDrawLine(d, pm, gc, 15, 9, 21, 3);
        XDrawLine(d, pm, gc, 15, 21, 21, 15);
        XDrawLine(d, pm, gc, 9, 3, 21, 3);
        XDrawLine(d, pm, gc, 21, 3, 21, 15);
        break;
    case ICON_TEXTURE:
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++)
                if ((x + y) & 1)
                    XFillRectangle(d, pm, gc, 4 + x * 4, 4 + y * 4, 4, 4);
        XDrawRectangle(d, pm, gc, 3, 3, 16, 16);
        XDrawLine(d, pm, gc, 14, 22, 22, 14);
        break;
    case ICON_CLIP:
        XDrawArc(d, pm, gc, 3, 15, 6, 6, 0, 360 * 64);
        XDrawArc(d, pm, gc, 14, 15, 6, 6, 0, 360 * 64);
        XDrawLine(d, pm, gc, 8, 15, 17, 3);
        XDrawLine(d, pm, gc, 15, 15, 6, 3);
        XDrawLine(d, pm, gc, 1, 11, 23, 11);
        break;
    case ICON_VERTEX:
        /* a polygon with handles on its corners */
        XDrawLine(d, pm, gc, 5, 19, 12, 5);
        XDrawLine(d, pm, gc, 12, 5, 19, 17);
        XDrawLine(d, pm, gc, 19, 17, 5, 19);
        XFillRectangle(d, pm, gc, 3, 17, 5, 5);
        XFillRectangle(d, pm, gc, 10, 3, 5, 5);
        XFillRectangle(d, pm, gc, 17, 15, 5, 5);
        break;
    default:
        break;
    }
    XFreeGC(d, gc);
    return pm;
}
