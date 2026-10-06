/*
 * ui_main.c - Motif main window: menus, tool palette, object bar, the four
 * OpenGL viewports, console and status bar
 *
 *  +---------------------------------------------------------------+
 *  | menu bar                                                      |
 *  | top tool bar                                                  |
 *  +----+---------------------------+---------------------------+--+
 *  |tool| camera (3D)               | top (x/y)                 |ob|
 *  |pal.+---------------------------+---------------------------+je|
 *  |    | front (y/z)               | side (x/z)                |ct|
 *  |    +---------------------------+---------------------------+ba|
 *  |    | console                                               |r |
 *  +----+-------------------------------------------------------+--+
 *  | status bar                                                    |
 *  +---------------------------------------------------------------+
 */
#include "actions.h"
#include "editor.h"
#include "eclass.h"
#include "game.h"
#include "render.h"
#include "textures.h"
#include "ui.h"
#include "ui_internal.h"
#include "view.h"

#include <ctype.h>
#include <locale.h>
#include <stdarg.h>
#include <sys/time.h>

#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <Xm/AtomMgr.h>
#include <Xm/CascadeB.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MainW.h>
#include <Xm/PanedW.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

XtAppContext app;
Display *dpy;
Widget toplevel;
XVisualInfo *glvis;
GLXContext glctx;
Colormap glcmap;

static Widget mainwin, workform, menubar, topbar, toolpal, sidebar, statusbar;
static Widget paned_main, viewgrid, console;
static Widget status_label, sel_label, grid_label, info_label;
static Widget tool_buttons[TOOL_COUNT];
static Widget preview_area, tex_label, class_list, sides_field;
static Widget snap_toggle, texlock_toggle, groups_toggle;
static Widget view_menu, ctx_menu, ctx_entity_item;
static view_t *popup_view;
static XButtonEvent last_bev;
static Cursor hidden_cursor, cross_cursor;
static int gl_ready;
static view_t *maximized;
static char **class_names;
static int nclass_names;

static String fallbacks[] = {
    "Qotif*background: #d4d0c8",
    "Qotif*foreground: black",
    "Qotif*XmText.background: white",
    "Qotif*XmTextField.background: white",
    "Qotif*XmList.background: white",
    "Qotif*console.background: #f4f4f4",
    "Qotif*highlightThickness: 1",
    "Qotif*toolTipEnable: True",
    "Qotif*XmPanedWindow.sashWidth: 8",
    "Qotif*XmPanedWindow.sashHeight: 8",
    NULL
};

/* ------------------------------------------------------------------ */
/* small helpers                                                       */

Cardinal ui_visual_args(Arg *args, Cardinal n)
{
    XtSetArg(args[n], XmNvisual, glvis->visual); n++;
    XtSetArg(args[n], XmNdepth, glvis->depth); n++;
    XtSetArg(args[n], XmNcolormap, glcmap); n++;
    return n;
}

XmString xms(const char *s)
{
    /* LtoR turns '\n' into line separators for multi-line labels */
    return XmStringCreateLtoR((char *)(s ? s : ""), XmFONTLIST_DEFAULT_TAG);
}

void ui_set_label(Widget w, const char *text)
{
    XmString s = xms(text);
    XtVaSetValues(w, XmNlabelString, s, NULL);
    XmStringFree(s);
}

char *ui_text_get(Widget w, char *buf, size_t size)
{
    char *s = XmIsTextField(w) ? XmTextFieldGetString(w) : XmTextGetString(w);
    str_copy(buf, s ? s : "", size);
    if (s)
        XtFree(s);
    return buf;
}

void ui_text_set(Widget w, const char *text)
{
    if (XmIsTextField(w))
        XmTextFieldSetString(w, (char *)(text ? text : ""));
    else
        XmTextSetString(w, (char *)(text ? text : ""));
}

Widget ui_button(Widget parent, const char *label, XtCallbackProc cb, XtPointer data)
{
    XmString s = xms(label);
    Widget w = XtVaCreateManagedWidget("button", xmPushButtonWidgetClass, parent,
                                       XmNlabelString, s, NULL);
    XmStringFree(s);
    if (cb)
        XtAddCallback(w, XmNactivateCallback, cb, data);
    return w;
}

Widget ui_form_dialog(const char *name, const char *title)
{
    Arg args[8];
    Cardinal n = ui_visual_args(args, 0);
    Widget d;

    XtSetArg(args[n], XmNautoUnmanage, False); n++;
    XtSetArg(args[n], XmNdialogStyle, XmDIALOG_MODELESS); n++;
    d = XmCreateFormDialog(toplevel, (char *)name, args, n);
    XtVaSetValues(XtParent(d), XmNtitle, title, NULL);
    return d;
}

static void gl_init(void)
{
    XFontStruct *font;
    unsigned int base;

    if (gl_ready)
        return;
    gl_ready = 1;
    render_init();
    font = XLoadQueryFont(dpy, prefs.font);
    if (!font)
        font = XLoadQueryFont(dpy, "fixed");
    if (font) {
        base = glGenLists(96);
        glXUseXFont(font->fid, 32, 96, (int)base);
        render_set_font(base, font->max_bounds.width, font->ascent + font->descent);
    }
    log_info("OpenGL: %s / %s", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
}

int ui_gl_begin(Widget w)
{
    if (!w || !XtIsRealized(w) || !XtIsManaged(w))
        return 0;
    if (!glXMakeCurrent(dpy, XtWindow(w), glctx))
        return 0;
    gl_init();
    return 1;
}

void ui_gl_end(Widget w)
{
    glXSwapBuffers(dpy, XtWindow(w));
}

/* ------------------------------------------------------------------ */
/* redraw scheduling                                                   */

enum { DIRTY_PREVIEW = 1 << 8, DIRTY_BROWSER = 1 << 9 };
static unsigned dirty;
static int redraw_pending;

static void draw_view(view_t *v)
{
    Widget w = (Widget)v->ui;
    if (!ui_gl_begin(w))
        return;
    render_view(v);
    ui_gl_end(w);
}

static Boolean redraw_proc(XtPointer cd)
{
    unsigned d = dirty;
    int i;

    (void)cd;
    redraw_pending = 0;
    dirty = 0;
    for (i = 0; i < MAX_VIEWS; i++)
        if (d & (1u << i))
            draw_view(&views[i]);
    if ((d & DIRTY_PREVIEW) && ui_gl_begin(preview_area)) {
        Dimension w, h;
        XtVaGetValues(preview_area, XmNwidth, &w, XmNheight, &h, NULL);
        render_texture_preview(w, h);
        ui_gl_end(preview_area);
    }
    if (d & DIRTY_BROWSER)
        dlg_redraw_browser();
    return True;
}

static void schedule(unsigned bits)
{
    dirty |= bits;
    if (!redraw_pending && app) {
        redraw_pending = 1;
        XtAppAddWorkProc(app, redraw_proc, NULL);
    }
}

void ui_redraw_all(void)
{
    schedule(0xFu | DIRTY_PREVIEW);
}

void ui_redraw_view(view_t *v)
{
    schedule(1u << v->index);
}

void ui_request_redraw_browser(void)
{
    schedule(DIRTY_BROWSER);
}

void ui_request_redraw_preview(void)
{
    schedule(DIRTY_PREVIEW);
}

/* ------------------------------------------------------------------ */
/* status and notifications                                            */

void ui_status(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (status_label)
        ui_set_label(status_label, buf);
}

static void console_log(int level, const char *msg)
{
    XmTextPosition end;
    char line[4200];

    if (!console)
        return;
    snprintf(line, sizeof(line), "%s%s\n", level == LOG_WARN ? "warning: " : level == LOG_ERROR ? "error: " : "", msg);
    end = XmTextGetLastPosition(console);
    if (end > 200000) {
        XmTextReplace(console, 0, end / 2, "");
        end = XmTextGetLastPosition(console);
    }
    XmTextInsert(console, end, line);
    XmTextShowPosition(console, XmTextGetLastPosition(console));
}

static void update_selection_info(void)
{
    char buf[256];
    vec3_t mins, maxs;
    int n = sel_count(), nf = sel_face_count();

    if (n && sel_bounds(mins, maxs))
        snprintf(buf, sizeof(buf), "%d selected\n%g x %g x %g", n,
                 maxs[0] - mins[0], maxs[1] - mins[1], maxs[2] - mins[2]);
    else
        snprintf(buf, sizeof(buf), "Nothing selected\n ");
    if (nf) {
        size_t l = strlen(buf);
        snprintf(buf + l, sizeof(buf) - l, "\n%d face(s)", nf);
    }
    if (info_label)
        ui_set_label(info_label, buf);
    snprintf(buf, sizeof(buf), "%d object(s) selected", n);
    if (sel_label)
        ui_set_label(sel_label, buf);
}

void ui_selection_changed(void)
{
    update_selection_info();
    dlg_selection_changed();
}

void ui_map_changed(void)
{
    char title[PATH_LEN + 64];
    const char *name = ed.map->path[0] ? path_basename(ed.map->path) : "untitled.map";

    snprintf(title, sizeof(title), "%s%s - %s [%s%s]", name, ed.map->dirty ? " *" : "",
             QOTIF_NAME, map_game_name, ed.map->valve220 ? ", Valve 220" : "");
    if (toplevel)
        XtVaSetValues(toplevel, XmNtitle, title, XmNiconName, name, NULL);
}

void ui_tool_changed(void)
{
    int i;
    static const char *names[TOOL_COUNT] = {
        "Selection", "Camera", "Entity", "Block", "Texture application", "Clipping", "Vertex"
    };
    for (i = 0; i < TOOL_COUNT; i++)
        if (tool_buttons[i])
            XmToggleButtonSetState(tool_buttons[i], i == (int)ed.tool, False);
    ui_status("%s tool", names[ed.tool]);
}

void ui_textures_changed(void)
{
    if (tex_label) {
        char buf[128];
        texture_t *t = tex_find(ed.texture);
        if (t)
            snprintf(buf, sizeof(buf), "%s  (%dx%d)", t->name, t->width, t->height);
        else
            snprintf(buf, sizeof(buf), "%s", ed.texture[0] ? ed.texture : "(no texture)");
        ui_set_label(tex_label, buf);
    }
    ui_request_redraw_preview();
    dlg_textures_changed();
}

static int cmp_names(const void *a, const void *b)
{
    return str_icmp(*(char *const *)a, *(char *const *)b);
}

void ui_entities_changed(void)
{
    XmString *items;
    int i, sel = 0;

    if (!class_list)
        return;
    free(class_names);
    class_names = NULL;
    nclass_names = 0;
    for (i = 0; i < eclass_count(); i++) {
        eclass_t *ec = eclass_get(i);
        if (ec->kind != EC_POINT)
            continue;
        class_names = xrealloc(class_names, sizeof(char *) * (size_t)(nclass_names + 1));
        class_names[nclass_names++] = ec->name;
    }
    qsort(class_names, (size_t)nclass_names, sizeof(char *), cmp_names);
    items = xmalloc(sizeof(XmString) * (size_t)(nclass_names + 1));
    for (i = 0; i < nclass_names; i++) {
        items[i] = xms(class_names[i]);
        if (str_ieq(class_names[i], ed.entclass))
            sel = i + 1;
    }
    XtVaSetValues(class_list, XmNitems, items, XmNitemCount, nclass_names, NULL);
    for (i = 0; i < nclass_names; i++)
        XmStringFree(items[i]);
    free(items);
    if (sel) {
        XmListSelectPos(class_list, sel, False);
        XmListSetBottomPos(class_list, sel);
    }
    dlg_entities_changed();
}

void ui_sync_toggles(void)
{
    int i;
    char buf[64];

    for (i = 0; i < num_actions; i++)
        if (actions[i].toggle && actions[i].widget)
            XmToggleButtonSetState((Widget)actions[i].widget, *actions[i].toggle ? True : False, False);
    if (snap_toggle)
        XmToggleButtonSetState(snap_toggle, ed.snap ? True : False, False);
    if (texlock_toggle)
        XmToggleButtonSetState(texlock_toggle, ed.texlock ? True : False, False);
    if (groups_toggle)
        XmToggleButtonSetState(groups_toggle, ed.ignore_groups ? True : False, False);
    snprintf(buf, sizeof(buf), "Grid: %d%s", ed.grid, ed.snap ? "" : " (no snap)");
    if (grid_label)
        ui_set_label(grid_label, buf);
    prefs.snap = ed.snap;
    prefs.show_grid = ed.show_grid;
    prefs.texlock = ed.texlock;
}

void ui_refresh_accelerators(void)
{
    int i;
    for (i = 0; i < num_actions; i++) {
        char buf[128];
        XmString s;
        if (!actions[i].widget)
            continue;
        keybind_format(&actions[i], buf, sizeof(buf));
        s = xms(buf);
        XtVaSetValues((Widget)actions[i].widget, XmNacceleratorText, s, NULL);
        XmStringFree(s);
    }
    dlg_refresh_accelerators();
}

/* ------------------------------------------------------------------ */
/* layout                                                              */

/*
 * The views sit in an XmForm (fractionBase 1000) attached to the split
 * positions, so they always share the space proportionally when the window
 * is resized.  The gap between them is a Hammer-style splitter: dragging it
 * moves the vertical split, the horizontal split, or both at the crossing.
 */
#define SPLIT_BASE 1000
#define SPLIT_GAP 4
#define SPLIT_GRAB 6

enum { SPLIT_X = 1, SPLIT_Y = 2 };
static int splitting;
static Cursor split_cursors[4];

static int layout_has_split(int which)
{
    if (maximized || prefs.layout == LAYOUT_ONE)
        return 0;
    if (which == SPLIT_Y)
        return prefs.layout == LAYOUT_FOUR;
    return 1;
}

static Cardinal attach_side(Arg *a, Cardinal n, String attach, String position, String offset, int frac)
{
    if (frac <= 0 || frac >= SPLIT_BASE) {
        XtSetArg(a[n], attach, XmATTACH_FORM); n++;
        XtSetArg(a[n], offset, 0); n++;
    } else {
        XtSetArg(a[n], attach, XmATTACH_POSITION); n++;
        XtSetArg(a[n], position, frac); n++;
        XtSetArg(a[n], offset, SPLIT_GAP); n++;
    }
    return n;
}

static void apply_layout(void)
{
    int i;
    int show[MAX_VIEWS] = { 1, 1, 1, 1 };

    if (maximized) {
        for (i = 0; i < MAX_VIEWS; i++)
            show[i] = &views[i] == maximized;
    } else if (prefs.layout == LAYOUT_TWO) {
        show[2] = show[3] = 0;
    } else if (prefs.layout == LAYOUT_ONE) {
        show[1] = show[2] = show[3] = 0;
    }
    for (i = 0; i < MAX_VIEWS; i++) {
        Widget w = (Widget)views[i].ui;
        int x0 = 0, x1 = SPLIT_BASE, y0 = 0, y1 = SPLIT_BASE;
        Arg a[16];
        Cardinal n = 0;

        if (!show[i]) {
            XtUnmanageChild(w);
            continue;
        }
        if (layout_has_split(SPLIT_X)) {
            x0 = (i % 2) ? prefs.split_x : 0;
            x1 = (i % 2) ? SPLIT_BASE : prefs.split_x;
        }
        if (layout_has_split(SPLIT_Y)) {
            y0 = (i / 2) ? prefs.split_y : 0;
            y1 = (i / 2) ? SPLIT_BASE : prefs.split_y;
        }
        n = attach_side(a, n, XmNleftAttachment, XmNleftPosition, XmNleftOffset, x0);
        n = attach_side(a, n, XmNrightAttachment, XmNrightPosition, XmNrightOffset, x1);
        n = attach_side(a, n, XmNtopAttachment, XmNtopPosition, XmNtopOffset, y0);
        n = attach_side(a, n, XmNbottomAttachment, XmNbottomPosition, XmNbottomOffset, y1);
        XtSetValues(w, a, n);
        XtManageChild(w);
    }
    /* repaint the divider bars at their new place */
    if (XtIsRealized(viewgrid))
        XClearArea(dpy, XtWindow(viewgrid), 0, 0, 0, 0, True);
    ui_redraw_all();
}

/* the gaps between views are gray bars in the Motif background color with
 * a light and a dark edge, like a sash */
static void draw_splitters(void)
{
    static GC gc;
    Pixel top, bottom;
    Dimension w, h;
    Window win;

    if (!XtIsRealized(viewgrid))
        return;
    win = XtWindow(viewgrid);
    if (!gc)
        gc = XCreateGC(dpy, win, 0, NULL);
    XtVaGetValues(viewgrid, XmNwidth, &w, XmNheight, &h,
                  XmNtopShadowColor, &top, XmNbottomShadowColor, &bottom, NULL);
    if (layout_has_split(SPLIT_X)) {
        int x = prefs.split_x * (int)w / SPLIT_BASE;
        XSetForeground(dpy, gc, top);
        XDrawLine(dpy, win, gc, x - SPLIT_GAP, 0, x - SPLIT_GAP, (int)h);
        XSetForeground(dpy, gc, bottom);
        XDrawLine(dpy, win, gc, x + SPLIT_GAP - 1, 0, x + SPLIT_GAP - 1, (int)h);
    }
    if (layout_has_split(SPLIT_Y)) {
        int y = prefs.split_y * (int)h / SPLIT_BASE;
        XSetForeground(dpy, gc, top);
        XDrawLine(dpy, win, gc, 0, y - SPLIT_GAP, (int)w, y - SPLIT_GAP);
        XSetForeground(dpy, gc, bottom);
        XDrawLine(dpy, win, gc, 0, y + SPLIT_GAP - 1, (int)w, y + SPLIT_GAP - 1);
    }
}

static int split_hit(int x, int y)
{
    Dimension w, h;
    int hit = 0;

    XtVaGetValues(viewgrid, XmNwidth, &w, XmNheight, &h, NULL);
    if (layout_has_split(SPLIT_X) && abs(x - prefs.split_x * (int)w / SPLIT_BASE) <= SPLIT_GRAB)
        hit |= SPLIT_X;
    if (layout_has_split(SPLIT_Y) && abs(y - prefs.split_y * (int)h / SPLIT_BASE) <= SPLIT_GRAB)
        hit |= SPLIT_Y;
    return hit;
}

static void splitter_event(Widget wid, XtPointer cd, XEvent *ev, Boolean *cont)
{
    Dimension w, h;
    (void)cd;
    (void)cont;

    if (!XtIsRealized(wid))
        return;
    switch (ev->type) {
    case ButtonPress:
        if (ev->xbutton.button == Button1)
            splitting = split_hit(ev->xbutton.x, ev->xbutton.y);
        break;
    case ButtonRelease:
        if (splitting) {
            splitting = 0;
            ui_redraw_all();
        }
        break;
    case MotionNotify:
        if (!splitting) {
            XDefineCursor(dpy, XtWindow(wid), split_cursors[split_hit(ev->xmotion.x, ev->xmotion.y)]);
            break;
        }
        XtVaGetValues(wid, XmNwidth, &w, XmNheight, &h, NULL);
        if ((splitting & SPLIT_X) && w > 0) {
            int s = ev->xmotion.x * SPLIT_BASE / (int)w;
            prefs.split_x = s < 100 ? 100 : s > 900 ? 900 : s;
        }
        if ((splitting & SPLIT_Y) && h > 0) {
            int s = ev->xmotion.y * SPLIT_BASE / (int)h;
            prefs.split_y = s < 100 ? 100 : s > 900 ? 900 : s;
        }
        apply_layout();
        break;
    case LeaveNotify:
        if (!splitting)
            XUndefineCursor(dpy, XtWindow(wid));
        break;
    case Expose:
        if (ev->xexpose.count == 0)
            draw_splitters();
        break;
    default:
        break;
    }
}

void ui_layout_changed(void)
{
    maximized = NULL;
    apply_layout();
}

void ui_maximize_view(view_t *v)
{
    if (!v)
        v = &views[0];
    maximized = maximized == v ? NULL : v;
    apply_layout();
}

/* ------------------------------------------------------------------ */
/* view widgets                                                        */

static int event_mods(unsigned int state)
{
    return ((state & ShiftMask) ? MOD_SHIFT : 0) | ((state & ControlMask) ? MOD_CTRL : 0)
         | ((state & Mod1Mask) ? MOD_ALT : 0);
}

static XtIntervalId fly_timer;
static struct timeval fly_last;

static void fly_tick(XtPointer cd, XtIntervalId *id)
{
    struct timeval now;
    double dt;
    Window root, child;
    int rx, ry, wx, wy;
    unsigned int mask = 0;

    (void)cd;
    (void)id;
    fly_timer = 0;
    if (!fly_keys)
        return;
    gettimeofday(&now, NULL);
    dt = (now.tv_sec - fly_last.tv_sec) + (now.tv_usec - fly_last.tv_usec) / 1e6;
    fly_last = now;
    if (dt > 0.1)
        dt = 0.1;
    XQueryPointer(dpy, XtWindow(toplevel), &root, &child, &rx, &ry, &wx, &wy, &mask);
    fly_step(dt, (mask & ShiftMask) != 0);
    fly_timer = XtAppAddTimeOut(app, 15, fly_tick, NULL);
}

void ui_fly_start(void)
{
    if (fly_timer)
        return;
    gettimeofday(&fly_last, NULL);
    fly_timer = XtAppAddTimeOut(app, 15, fly_tick, NULL);
}

static int is_autorepeat(XEvent *ev)
{
    XEvent next;
    if (!XEventsQueued(dpy, QueuedAfterReading))
        return 0;
    XPeekEvent(dpy, &next);
    return next.type == KeyPress && next.xkey.keycode == ev->xkey.keycode
        && next.xkey.time - ev->xkey.time < 2;
}

static void view_key(view_t *v, XEvent *ev)
{
    KeySym sym = XLookupKeysym(&ev->xkey, 0), lower, upper;
    int mods = event_mods(ev->xkey.state);
    action_t *a;

    XConvertCase(sym, &lower, &upper);
    sym = lower;
    if (ev->type == KeyRelease) {
        a = fly_action_for_key(sym);
        if (a && !is_autorepeat(ev))
            fly_keys &= ~a->fly;
        return;
    }
    if (v->type == VIEW_3D && !(mods & (MOD_CTRL | MOD_ALT))) {
        a = fly_action_for_key(sym);
        if (a && (!(mods & MOD_SHIFT) || v->mode == MODE_LOOK || v == captured_view)) {
            fly_keys |= a->fly;
            ui_fly_start();
            return;
        }
    }
    a = action_for_key(mods, sym);
    if (a)
        action_run(a);
}

static void view_event(Widget w, XtPointer cd, XEvent *ev, Boolean *cont)
{
    view_t *v = (view_t *)cd;
    (void)cont;

    switch (ev->type) {
    case ButtonPress:
        XmProcessTraversal(w, XmTRAVERSE_CURRENT);
        last_bev = ev->xbutton;
        if (active_view != v) {
            active_view = v;
            ui_redraw_all();
        }
        if (ev->xbutton.button == Button4 || ev->xbutton.button == Button5) {
            view_wheel(v, ev->xbutton.button == Button4 ? 1 : -1, ev->xbutton.x, ev->xbutton.y,
                       event_mods(ev->xbutton.state));
            break;
        }
        if (ev->xbutton.button <= 3)
            view_mouse_down(v, (int)ev->xbutton.button, ev->xbutton.x, ev->xbutton.y,
                            event_mods(ev->xbutton.state));
        break;
    case ButtonRelease:
        last_bev = ev->xbutton;
        if (ev->xbutton.button <= 3)
            view_mouse_up(v, (int)ev->xbutton.button, ev->xbutton.x, ev->xbutton.y,
                          event_mods(ev->xbutton.state));
        break;
    case MotionNotify:
        view_mouse_move(v, ev->xmotion.x, ev->xmotion.y, event_mods(ev->xmotion.state));
        break;
    case KeyPress:
    case KeyRelease:
        view_key(v, ev);
        break;
    case EnterNotify:
        XmProcessTraversal(w, XmTRAVERSE_CURRENT);
        if (active_view != v && v->mode == MODE_NONE) {
            active_view = v;
            ui_redraw_all();
        }
        break;
    case LeaveNotify:
        if (v->mode != MODE_LOOK && v != captured_view)
            fly_keys = 0;
        view_mouse_leave(v);
        break;
    default:
        break;
    }
}

static void view_expose_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmDrawingAreaCallbackStruct *cbs = (XmDrawingAreaCallbackStruct *)cb;
    (void)w;
    if (cbs->event && cbs->event->type == Expose && cbs->event->xexpose.count > 0)
        return;
    ui_redraw_view((view_t *)cd);
}

static void view_resize_cb(Widget w, XtPointer cd, XtPointer cb)
{
    view_t *v = (view_t *)cd;
    Dimension width, height;
    (void)cb;
    XtVaGetValues(w, XmNwidth, &width, XmNheight, &height, NULL);
    v->width = width;
    v->height = height;
    ui_redraw_view(v);
}

void ui_set_cursor(view_t *v, int cursor)
{
    Widget w = (Widget)v->ui;
    if (!XtIsRealized(w))
        return;
    switch (cursor) {
    case CURSOR_HIDDEN:
        XDefineCursor(dpy, XtWindow(w), hidden_cursor);
        break;
    case CURSOR_CROSS:
        XDefineCursor(dpy, XtWindow(w), cross_cursor);
        break;
    default:
        XUndefineCursor(dpy, XtWindow(w));
        break;
    }
}

void ui_warp_pointer(view_t *v, int x, int y)
{
    Widget w = (Widget)v->ui;
    if (XtIsRealized(w))
        XWarpPointer(dpy, None, XtWindow(w), 0, 0, 0, 0, x, y);
}

/* confines the pointer to a view (hidden) for mouse capture */
int ui_grab_pointer(view_t *v, int grab)
{
    Widget w = (Widget)v->ui;

    if (!grab) {
        XUngrabPointer(dpy, CurrentTime);
        return 1;
    }
    if (!XtIsRealized(w) || !XtIsManaged(w))
        return 0;
    XmProcessTraversal(w, XmTRAVERSE_CURRENT);
    return XGrabPointer(dpy, XtWindow(w), False,
                        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                        GrabModeAsync, GrabModeAsync, XtWindow(w), hidden_cursor,
                        CurrentTime) == GrabSuccess;
}

void ui_console_changed(void)
{
    Widget sw = XtParent(console);
    if (prefs.show_console)
        XtManageChild(sw);
    else
        XtUnmanageChild(sw);
}

static Widget make_view_widget(Widget parent, view_t *v)
{
    Widget w = XtVaCreateManagedWidget("view", xmDrawingAreaWidgetClass, parent,
                                       XmNwidth, 420, XmNheight, 300,
                                       XmNtraversalOn, True,
                                       XmNresizePolicy, XmRESIZE_NONE,
                                       XmNmarginWidth, 0, XmNmarginHeight, 0,
                                       NULL);
    v->ui = w;
    v->width = 420;
    v->height = 300;
    XtAddCallback(w, XmNexposeCallback, view_expose_cb, v);
    XtAddCallback(w, XmNresizeCallback, view_resize_cb, v);
    XtAddEventHandler(w, ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask
                      | KeyReleaseMask | EnterWindowMask | LeaveWindowMask,
                      False, view_event, v);
    return w;
}

/* ------------------------------------------------------------------ */
/* popup menus                                                         */

static void view_menu_cb(Widget w, XtPointer cd, XtPointer cb)
{
    int item = (int)(long)cd;
    (void)w;
    (void)cb;
    if (!popup_view)
        return;
    if (item < VIEW_TYPE_COUNT) {
        int keep_zoom = popup_view->type != VIEW_3D;
        view_set_type(popup_view, (viewtype_t)item);
        if (!keep_zoom && item != VIEW_3D)
            popup_view->zoom = 0.5;
        prefs.view_types[popup_view->index] = item;
    } else {
        popup_view->rmode = (rendermode_t)(item - VIEW_TYPE_COUNT);
        if (popup_view->type != VIEW_3D) {
            view_set_type(popup_view, VIEW_3D);
            prefs.view_types[popup_view->index] = VIEW_3D;
        }
    }
    ui_redraw_all();
}

/*
 * Popup menus are posted by hand (XmMenuPosition + XtManageChild).  Motif
 * also installs a passive Button3 grab on the popup's parent so it can post
 * itself; if that parent were an ancestor of the views (workform), every
 * right click in a view would be redirected to it and the views would never
 * see Button3.  The status bar is not an ancestor of anything we need.
 */
static Widget make_popup(const char *name)
{
    Arg args[8];
    Cardinal n = ui_visual_args(args, 0);
#ifdef XmPOPUP_KEYBOARD
    XtSetArg(args[n], XmNpopupEnabled, XmPOPUP_KEYBOARD); n++;
#endif
    return XmCreatePopupMenu(statusbar, (char *)name, args, n);
}

static void build_view_menu(void)
{
    static const char *items[] = {
        "3D Camera", "2D Top (X/Y)", "2D Front (Y/Z)", "2D Side (X/Z)",
        "-", "3D Textured", "3D Flat Shaded", "3D Wireframe"
    };
    int i, id = 0;

    view_menu = make_popup("viewmenu");
    for (i = 0; i < (int)ARRAY_COUNT(items); i++) {
        if (items[i][0] == '-') {
            XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, view_menu, NULL);
            continue;
        }
        ui_button(view_menu, items[i], view_menu_cb, (XtPointer)(long)id);
        id++;
    }
}

void ui_popup_view_menu(view_t *v, int x, int y)
{
    (void)x;
    (void)y;
    popup_view = v;
    XmMenuPosition(view_menu, &last_bev);
    XtManageChild(view_menu);
}

enum { CTX_ENTITY, CTX_CAMERA, CTX_CENTER, CTX_ACTION };

static void ctx_cb(Widget w, XtPointer cd, XtPointer cb)
{
    long item = (long)cd;
    view_t *v = popup_view;
    (void)w;
    (void)cb;

    if (!v)
        return;
    if (item == CTX_ENTITY) {
        eclass_t *ec = eclass_find(ed.entclass);
        vec3_t o, mins, maxs;
        if (!ec || ec->kind != EC_POINT) {
            ui_status("Choose a point entity class in the object bar first");
            return;
        }
        v3_copy(v->ctx_point, o);
        if (sel_bounds(mins, maxs))
            o[v->dax] = floor((mins[v->dax] + maxs[v->dax]) * 0.5 + 0.5);
        else if (v->dax == 2)
            o[2] = -ec->mins[2];
        cmd_create_entity(ec->name, o);
    } else if (item == CTX_CAMERA) {
        ed.cam.pos[v->hax] = v->ctx_point[v->hax];
        ed.cam.pos[v->vax] = v->ctx_point[v->vax];
        ui_redraw_all();
    } else if (item == CTX_CENTER) {
        int i;
        /* only the axes the clicked view knows about are changed */
        for (i = 0; i < MAX_VIEWS; i++) {
            if (views[i].type == VIEW_3D)
                continue;
            if (views[i].hax == v->hax || views[i].hax == v->vax)
                views[i].cx = v->ctx_point[views[i].hax];
            if (views[i].vax == v->hax || views[i].vax == v->vax)
                views[i].cy = v->ctx_point[views[i].vax];
        }
        ui_redraw_all();
    }
}

static void ctx_action_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cb;
    action_run((action_t *)cd);
}

static void build_context_menu(void)
{
    static const char *acts[] = { "paste", "-", "tie", "to_world", "carve", "-", "delete", "properties" };
    int i;

    ctx_menu = make_popup("contextmenu");
    ctx_entity_item = ui_button(ctx_menu, "Create Entity Here", ctx_cb, (XtPointer)CTX_ENTITY);
    ui_button(ctx_menu, "Move Camera Here", ctx_cb, (XtPointer)CTX_CAMERA);
    ui_button(ctx_menu, "Center 2D Views Here", ctx_cb, (XtPointer)CTX_CENTER);
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, ctx_menu, NULL);
    for (i = 0; i < (int)ARRAY_COUNT(acts); i++) {
        action_t *a;
        if (acts[i][0] == '-') {
            XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, ctx_menu, NULL);
            continue;
        }
        a = action_find(acts[i]);
        if (a)
            ui_button(ctx_menu, a->label, ctx_action_cb, a);
    }
}

void ui_popup_context(view_t *v, int x, int y)
{
    char buf[128];
    (void)x;
    (void)y;
    popup_view = v;
    snprintf(buf, sizeof(buf), "Create %s Here", ed.entclass[0] ? ed.entclass : "Entity");
    ui_set_label(ctx_entity_item, buf);
    XmMenuPosition(ctx_menu, &last_bev);
    XtManageChild(ctx_menu);
}

/* ------------------------------------------------------------------ */
/* menus                                                               */

static void action_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cb;
    action_run((action_t *)cd);
}

static Widget add_menu(const char *title, char mnemonic, const char **items)
{
    Arg args[8];
    Cardinal n = ui_visual_args(args, 0);
    Widget pd = XmCreatePulldownMenu(menubar, "pulldown", args, n);
    XmString s = xms(title);
    Widget cascade;
    int i;

    XtVaSetValues(pd, XmNtearOffModel, XmTEAR_OFF_ENABLED, NULL);
    cascade = XtVaCreateManagedWidget(title, xmCascadeButtonWidgetClass, menubar,
                                      XmNsubMenuId, pd, XmNlabelString, s,
                                      XmNmnemonic, (KeySym)mnemonic, NULL);
    XmStringFree(s);
    for (i = 0; items[i]; i++) {
        action_t *a;
        char acc[128];
        XmString lbl, acct;
        Widget w;

        if (items[i][0] == '-') {
            XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, pd, NULL);
            continue;
        }
        a = action_find(items[i]);
        if (!a) {
            log_warn("menu: unknown action '%s'", items[i]);
            continue;
        }
        keybind_format(a, acc, sizeof(acc));
        lbl = xms(a->label);
        acct = xms(acc);
        if (a->toggle) {
            w = XtVaCreateManagedWidget(a->name, xmToggleButtonWidgetClass, pd,
                                        XmNlabelString, lbl, XmNacceleratorText, acct,
                                        XmNvisibleWhenOff, True,
                                        XmNset, *a->toggle ? True : False, NULL);
            XtAddCallback(w, XmNvalueChangedCallback, action_cb, a);
        } else {
            w = XtVaCreateManagedWidget(a->name, xmPushButtonWidgetClass, pd,
                                        XmNlabelString, lbl, XmNacceleratorText, acct, NULL);
            XtAddCallback(w, XmNactivateCallback, action_cb, a);
        }
        XmStringFree(lbl);
        XmStringFree(acct);
        a->widget = w;
    }
    return cascade;
}

static void build_menus(void)
{
    static const char *file_items[] = {
        "new", "open", "save", "save_as", "-", "pointfile_load", "pointfile_clear", "-",
        "compile", "-", "quit", NULL
    };
    static const char *edit_items[] = {
        "undo", "redo", "-", "cut", "copy", "paste", "duplicate", "delete", "-",
        "select_all", "select_none", "select_invert", "-", "properties", "face_edit", "-",
        "preferences", NULL
    };
    static const char *view_items[] = {
        "grid_smaller", "grid_larger", "toggle_grid", "toggle_snap", "-",
        "center_2d", "center_3d", "-", "layout_four", "layout_two", "layout_one", "view_maximize", "capture_mouse",
        "render_cycle", "-", "toggle_names", "toggle_links", "toggle_console", "-", "texture_browser", NULL
    };
    static const char *tools_items[] = {
        "tool_select", "tool_camera", "tool_entity", "tool_block", "tool_texture", "tool_clip",
        "tool_vertex", "-",
        "clip_apply", "clip_cycle", "-", "apply_texture", "toggle_texlock", "toggle_groups", NULL
    };
    static const char *map_items[] = {
        "tie", "to_world", "-", "carve", "hollow", "merge", "-", "flip_h", "flip_v",
        "rotate_cw", "rotate_ccw", "snap_selected", "-", "set_wads", "entity_defs",
        "convert_valve", "reload", "-", "goto_brush", "map_info", "check_map", NULL
    };
    static const char *help_items[] = { "keys_help", "about", NULL };
    Widget help;

    menubar = XmCreateMenuBar(mainwin, "menubar", NULL, 0);
    add_menu("File", 'F', file_items);
    add_menu("Edit", 'E', edit_items);
    add_menu("View", 'V', view_items);
    add_menu("Tools", 'T', tools_items);
    add_menu("Map", 'M', map_items);
    help = add_menu("Help", 'H', help_items);
    XtVaSetValues(menubar, XmNmenuHelpWidget, help, NULL);
    XtManageChild(menubar);
}

/* ------------------------------------------------------------------ */
/* tool bars                                                           */

/*
 * The palette keeps exactly one button down by itself (ui_tool_changed sets
 * every button from ed.tool).  The RowColumn's own radioBehavior is not
 * used: it unsets the previous button with a callback of its own, which
 * raced with ours and could put the old tool back.
 */
static void tool_cb(Widget w, XtPointer cd, XtPointer cb)
{
    tool_t t = (tool_t)(long)cd;
    (void)w;
    (void)cb;
    if (t != ed.tool)
        ed_set_tool(t);
    else
        ui_tool_changed();   /* clicking the active tool keeps it pressed */
}

static void build_tool_palette(void)
{
    static const char *tips[TOOL_COUNT] = {
        "Selection tool (Shift+S)", "Camera tool (Shift+C)", "Entity tool (Shift+E)",
        "Block tool (Shift+B)", "Texture application tool (Shift+A)", "Clipping tool (Shift+X)",
        "Vertex tool: drag vertices, edges and faces (Shift+V)"
    };
    int i;

    toolpal = XtVaCreateManagedWidget("toolpalette", xmRowColumnWidgetClass, workform,
                                      XmNorientation, XmVERTICAL,
                                      XmNpacking, XmPACK_COLUMN,
                                      XmNspacing, 2,
                                      XmNmarginWidth, 3,
                                      NULL);
    for (i = 0; i < TOOL_COUNT; i++) {
        Widget b = XtVaCreateManagedWidget("tool", xmToggleButtonWidgetClass, toolpal,
                                           XmNindicatorOn, False,
                                           XmNshadowThickness, 2,
                                           XmNfillOnSelect, True,
                                           XmNset, i == (int)ed.tool ? True : False,
                                           XmNmarginWidth, 3, XmNmarginHeight, 3,
                                           NULL);
        Pixmap pm = ui_make_icon(b, i);
        XtVaSetValues(b, XmNlabelType, XmPIXMAP, XmNlabelPixmap, pm, XmNselectPixmap, pm, NULL);
#ifdef XmNtoolTipString
        {
            XmString s = xms(tips[i]);
            XtVaSetValues(b, XmNtoolTipString, s, NULL);
            XmStringFree(s);
        }
#else
        (void)tips;
#endif
        XtAddCallback(b, XmNvalueChangedCallback, tool_cb, (XtPointer)(long)i);
        tool_buttons[i] = b;
    }
}

static void toggle_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)cb;
    int *var = (int *)cd;
    (void)w;
    *var = cbs->set ? 1 : 0;
    ui_sync_toggles();
    ui_redraw_all();
}

static Widget bar_toggle(Widget parent, const char *label, int *var)
{
    XmString s = xms(label);
    Widget w = XtVaCreateManagedWidget("toggle", xmToggleButtonWidgetClass, parent,
                                       XmNlabelString, s, XmNset, *var ? True : False, NULL);
    XmStringFree(s);
    XtAddCallback(w, XmNvalueChangedCallback, toggle_cb, var);
    return w;
}

static void bar_action(Widget parent, const char *label, const char *action)
{
    action_t *a = action_find(action);
    Widget b;
    if (!a)
        return;
    b = ui_button(parent, label, action_cb, a);
#ifdef XmNtoolTipString
    {
        char tip[160], keys[96];
        XmString s;
        keybind_format(a, keys, sizeof(keys));
        snprintf(tip, sizeof(tip), keys[0] ? "%s (%s)" : "%s", a->label, keys);
        s = xms(tip);
        XtVaSetValues(b, XmNtoolTipString, s, NULL);
        XmStringFree(s);
    }
#endif
}

static void bar_sep(Widget parent)
{
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, parent,
                            XmNorientation, XmVERTICAL, XmNwidth, 8, NULL);
}

static void build_top_bar(void)
{
    topbar = XtVaCreateManagedWidget("topbar", xmRowColumnWidgetClass, workform,
                                     XmNorientation, XmHORIZONTAL,
                                     XmNspacing, 2, XmNmarginHeight, 2,
                                     NULL);
    bar_action(topbar, "New", "new");
    bar_action(topbar, "Open", "open");
    bar_action(topbar, "Save", "save");
    bar_sep(topbar);
    bar_action(topbar, "Undo", "undo");
    bar_action(topbar, "Redo", "redo");
    bar_sep(topbar);
    bar_action(topbar, "Cut", "cut");
    bar_action(topbar, "Copy", "copy");
    bar_action(topbar, "Paste", "paste");
    bar_sep(topbar);
    bar_action(topbar, "Grid -", "grid_smaller");
    bar_action(topbar, "Grid +", "grid_larger");
    snap_toggle = bar_toggle(topbar, "Snap", &ed.snap);
    texlock_toggle = bar_toggle(topbar, "Tex Lock", &ed.texlock);
    groups_toggle = bar_toggle(topbar, "Ignore Groups", &ed.ignore_groups);
    bar_sep(topbar);
    bar_action(topbar, "Carve", "carve");
    bar_action(topbar, "Hollow", "hollow");
    bar_action(topbar, "Merge", "merge");
    bar_action(topbar, "Clip", "clip_apply");
    bar_sep(topbar);
    bar_action(topbar, "Flip H", "flip_h");
    bar_action(topbar, "Flip V", "flip_v");
    bar_action(topbar, "Rotate", "rotate_cw");
    bar_sep(topbar);
    bar_action(topbar, "Textures", "texture_browser");
    bar_action(topbar, "Properties", "properties");
    bar_action(topbar, "Run Map", "compile");
}

/* ------------------------------------------------------------------ */
/* object bar (right side)                                             */

static void preview_expose_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cd;
    (void)cb;
    ui_request_redraw_preview();
}

static void preview_input_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmDrawingAreaCallbackStruct *cbs = (XmDrawingAreaCallbackStruct *)cb;
    (void)w;
    (void)cd;
    if (cbs->event && cbs->event->type == ButtonPress)
        ui_open_texture_browser();
}

static void class_select_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)cb;
    (void)w;
    (void)cd;
    if (cbs->item_position >= 1 && cbs->item_position <= nclass_names) {
        str_copy(ed.entclass, class_names[cbs->item_position - 1], sizeof(ed.entclass));
        if (cbs->reason == XmCR_DEFAULT_ACTION)
            ed_set_tool(TOOL_ENTITY);
    }
}

static void prim_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cb;
    ed.prim = (int)(long)cd;
    if (ed.tool != TOOL_BLOCK)
        ed_set_tool(TOOL_BLOCK);
}

static void sides_cb(Widget w, XtPointer cd, XtPointer cb)
{
    char buf[16];
    int n;
    (void)cd;
    (void)cb;
    n = atoi(ui_text_get(w, buf, sizeof(buf)));
    if (n >= 3 && n <= 32)
        ed.prim_sides = n;
}

static Widget frame_box(Widget parent, const char *title)
{
    Widget frame = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, parent,
                                           XmNshadowType, XmSHADOW_ETCHED_IN, NULL);
    XmString s = xms(title);
    XtVaCreateManagedWidget("title", xmLabelWidgetClass, frame,
                            XmNlabelString, s,
                            XmNchildType, XmFRAME_TITLE_CHILD, NULL);
    XmStringFree(s);
    return XtVaCreateManagedWidget("box", xmRowColumnWidgetClass, frame,
                                   XmNorientation, XmVERTICAL,
                                   XmNchildType, XmFRAME_WORKAREA_CHILD,
                                   XmNmarginWidth, 4, XmNmarginHeight, 4, NULL);
}

static void build_sidebar(void)
{
    Widget box, row, pd, om;
    Arg args[12];
    Cardinal n;
    static const char *prims[PRIM_COUNT] = { "Block", "Wedge", "Cylinder", "Spike" };
    int i;
    XmString s;

    sidebar = XtVaCreateManagedWidget("objectbar", xmRowColumnWidgetClass, workform,
                                      XmNorientation, XmVERTICAL,
                                      XmNpacking, XmPACK_TIGHT,
                                      XmNspacing, 6,
                                      XmNmarginWidth, 4,
                                      NULL);

    box = frame_box(sidebar, "Current Texture");
    preview_area = XtVaCreateManagedWidget("texpreview", xmDrawingAreaWidgetClass, box,
                                           XmNwidth, 160, XmNheight, 128,
                                           XmNresizePolicy, XmRESIZE_NONE, NULL);
    XtAddCallback(preview_area, XmNexposeCallback, preview_expose_cb, NULL);
    XtAddCallback(preview_area, XmNinputCallback, preview_input_cb, NULL);
    s = xms("(no texture)");
    tex_label = XtVaCreateManagedWidget("texname", xmLabelWidgetClass, box,
                                        XmNlabelString, s, XmNalignment, XmALIGNMENT_BEGINNING, NULL);
    XmStringFree(s);
    row = XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, box,
                                  XmNorientation, XmHORIZONTAL, XmNmarginWidth, 0, NULL);
    bar_action(row, "Browse...", "texture_browser");
    bar_action(row, "Apply", "apply_texture");

    box = frame_box(sidebar, "Entities (point classes)");
    n = 0;
    XtSetArg(args[n], XmNvisibleItemCount, 12); n++;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNlistSizePolicy, XmCONSTANT); n++;
    XtSetArg(args[n], XmNwidth, 160); n++;
    XtSetArg(args[n], XmNscrollBarDisplayPolicy, XmSTATIC); n++;
    class_list = XmCreateScrolledList(box, "classes", args, n);
    XtAddCallback(class_list, XmNbrowseSelectionCallback, class_select_cb, NULL);
    XtAddCallback(class_list, XmNdefaultActionCallback, class_select_cb, NULL);
    XtManageChild(class_list);
    row = XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, box,
                                  XmNorientation, XmHORIZONTAL, XmNmarginWidth, 0, NULL);
    bar_action(row, "To Entity", "tie");
    bar_action(row, "To World", "to_world");

    box = frame_box(sidebar, "Brush");
    n = ui_visual_args(args, 0);
    pd = XmCreatePulldownMenu(box, "primpd", args, n);
    for (i = 0; i < PRIM_COUNT; i++)
        ui_button(pd, prims[i], prim_cb, (XtPointer)(long)i);
    s = xms("Type:");
    n = 0;
    XtSetArg(args[n], XmNsubMenuId, pd); n++;
    XtSetArg(args[n], XmNlabelString, s); n++;
    om = XmCreateOptionMenu(box, "primitive", args, n);
    XtManageChild(om);
    XmStringFree(s);
    row = XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, box,
                                  XmNorientation, XmHORIZONTAL, XmNmarginWidth, 0, NULL);
    s = xms("Sides:");
    XtVaCreateManagedWidget("label", xmLabelWidgetClass, row, XmNlabelString, s, NULL);
    XmStringFree(s);
    sides_field = XtVaCreateManagedWidget("sides", xmTextFieldWidgetClass, row,
                                          XmNcolumns, 4, XmNvalue, "8", NULL);
    XtAddCallback(sides_field, XmNvalueChangedCallback, sides_cb, NULL);

    box = frame_box(sidebar, "Selection");
    s = xms("Nothing selected\n ");
    info_label = XtVaCreateManagedWidget("selinfo", xmLabelWidgetClass, box,
                                         XmNlabelString, s, XmNalignment, XmALIGNMENT_BEGINNING, NULL);
    XmStringFree(s);
    bar_action(box, "Object Properties...", "properties");
    bar_action(box, "Face Properties...", "face_edit");
}

static void build_status_bar(void)
{
    XmString s = xms("Ready");
    statusbar = XtVaCreateManagedWidget("statusbar", xmFormWidgetClass, workform,
                                        XmNshadowType, XmSHADOW_IN, NULL);
    grid_label = XtVaCreateManagedWidget("grid", xmLabelWidgetClass, statusbar,
                                         XmNlabelString, s, XmNrecomputeSize, False, XmNwidth, 140,
                                         XmNrightAttachment, XmATTACH_FORM,
                                         XmNtopAttachment, XmATTACH_FORM,
                                         XmNbottomAttachment, XmATTACH_FORM, NULL);
    sel_label = XtVaCreateManagedWidget("sel", xmLabelWidgetClass, statusbar,
                                        XmNlabelString, s, XmNrecomputeSize, False, XmNwidth, 170,
                                        XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, grid_label,
                                        XmNtopAttachment, XmATTACH_FORM,
                                        XmNbottomAttachment, XmATTACH_FORM, NULL);
    status_label = XtVaCreateManagedWidget("status", xmLabelWidgetClass, statusbar,
                                           XmNlabelString, s, XmNrecomputeSize, False,
                                           XmNalignment, XmALIGNMENT_BEGINNING,
                                           XmNleftAttachment, XmATTACH_FORM,
                                           XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, sel_label,
                                           XmNtopAttachment, XmATTACH_FORM,
                                           XmNbottomAttachment, XmATTACH_FORM, NULL);
    XmStringFree(s);
}

static void build_views(void)
{
    Arg args[12];
    Cardinal n;
    int i;

    /* views on top, console below; the console keeps its height when the
     * window is resized so the extra space goes to the views */
    paned_main = XtVaCreateManagedWidget("paned", xmPanedWindowWidgetClass, workform,
                                         XmNorientation, XmVERTICAL, NULL);
    viewgrid = XtVaCreateManagedWidget("viewgrid", xmFormWidgetClass, paned_main,
                                       XmNfractionBase, SPLIT_BASE,
                                       XmNpaneMinimum, 120,
                                       XmNwidth, 840 + 2 * SPLIT_GAP,
                                       XmNheight, 600 + 2 * SPLIT_GAP,
                                       XmNresizePolicy, XmRESIZE_NONE,
                                       NULL);
    XtAddEventHandler(viewgrid, ButtonPressMask | ButtonReleaseMask | PointerMotionMask
                      | LeaveWindowMask | ExposureMask, False, splitter_event, NULL);
    split_cursors[0] = None;
    split_cursors[SPLIT_X] = XCreateFontCursor(dpy, XC_sb_h_double_arrow);
    split_cursors[SPLIT_Y] = XCreateFontCursor(dpy, XC_sb_v_double_arrow);
    split_cursors[SPLIT_X | SPLIT_Y] = XCreateFontCursor(dpy, XC_fleur);
    for (i = 0; i < MAX_VIEWS; i++) {
        view_init(&views[i], i, (viewtype_t)prefs.view_types[i]);
        make_view_widget(viewgrid, &views[i]);
    }
    active_view = &views[1];

    n = 0;
    XtSetArg(args[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
    XtSetArg(args[n], XmNeditable, False); n++;
    XtSetArg(args[n], XmNcursorPositionVisible, False); n++;
    XtSetArg(args[n], XmNrows, 5); n++;
    XtSetArg(args[n], XmNwordWrap, True); n++;
    XtSetArg(args[n], XmNscrollHorizontal, False); n++;
    XtSetArg(args[n], XmNtraversalOn, False); n++;
    console = XmCreateScrolledText(paned_main, "console", args, n);
    XtVaSetValues(XtParent(console), XmNskipAdjust, True, XmNpaneMinimum, 40, NULL);
    XtManageChild(console);
}

static void layout_workform(void)
{
    XtVaSetValues(topbar,
                  XmNtopAttachment, XmATTACH_FORM,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(statusbar,
                  XmNbottomAttachment, XmATTACH_FORM,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(toolpal,
                  XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, topbar,
                  XmNleftAttachment, XmATTACH_FORM, NULL);
    XtVaSetValues(sidebar,
                  XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, topbar,
                  XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, statusbar, NULL);
    XtVaSetValues(paned_main,
                  XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, topbar,
                  XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, toolpal,
                  XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, sidebar,
                  XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, statusbar,
                  XmNleftOffset, 2, XmNrightOffset, 2, NULL);
}

/* ------------------------------------------------------------------ */
/* visual selection and startup                                        */

static int choose_visual(void)
{
    int scr = DefaultScreen(dpy);
    XVisualInfo tmpl, *list;
    int n = 0;
    static int attr24[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 4, GLX_GREEN_SIZE, 4,
                            GLX_BLUE_SIZE, 4, GLX_DEPTH_SIZE, 24, None };
    static int attr16[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1,
                            GLX_BLUE_SIZE, 1, GLX_DEPTH_SIZE, 16, None };

    if (!glXQueryExtension(dpy, NULL, NULL)) {
        fprintf(stderr, "qotif: the X server has no GLX extension\n");
        return 0;
    }
    /* prefer the default visual so Motif widgets and GL share colormaps */
    tmpl.visualid = XVisualIDFromVisual(DefaultVisual(dpy, scr));
    tmpl.screen = scr;
    list = XGetVisualInfo(dpy, VisualIDMask | VisualScreenMask, &tmpl, &n);
    if (list && n > 0) {
        int use = 0, rgba = 0, db = 0, depth = 0;
        glXGetConfig(dpy, &list[0], GLX_USE_GL, &use);
        glXGetConfig(dpy, &list[0], GLX_RGBA, &rgba);
        glXGetConfig(dpy, &list[0], GLX_DOUBLEBUFFER, &db);
        glXGetConfig(dpy, &list[0], GLX_DEPTH_SIZE, &depth);
        if (use && rgba && db && depth >= 16) {
            glvis = list;
            glcmap = DefaultColormap(dpy, scr);
            return 1;
        }
        XFree(list);
    }
    glvis = glXChooseVisual(dpy, scr, attr24);
    if (!glvis)
        glvis = glXChooseVisual(dpy, scr, attr16);
    if (!glvis) {
        fprintf(stderr, "qotif: no double buffered RGBA visual with a depth buffer\n");
        return 0;
    }
    glcmap = XCreateColormap(dpy, RootWindow(dpy, scr), glvis->visual, AllocNone);
    return 1;
}

static void make_cursors(void)
{
    static char empty[8] = { 0 };
    XColor black;
    Pixmap pm = XCreateBitmapFromData(dpy, RootWindow(dpy, DefaultScreen(dpy)), empty, 8, 8);
    memset(&black, 0, sizeof(black));
    hidden_cursor = XCreatePixmapCursor(dpy, pm, pm, &black, &black, 0, 0);
    XFreePixmap(dpy, pm);
    cross_cursor = XCreateFontCursor(dpy, XC_crosshair);
}

static void wm_close_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cd;
    (void)cb;
    action_run_name("quit");
}

void ui_quit(void)
{
    int i;
    for (i = 0; i < MAX_VIEWS; i++)
        prefs.view_types[i] = views[i].type;
    prefs_save();
    exit(0);
}

unsigned long ui_keysym(const char *name)
{
    KeySym sym = XStringToKeysym((char *)name), lower, upper;

    if (sym == NoSymbol && strlen(name) == 1)
        sym = (KeySym)(unsigned char)name[0];
    if (sym == NoSymbol && name[0]) {
        /* accept "return" for "Return", "escape" for "Escape", ... */
        char tmp[64];
        str_copy(tmp, name, sizeof(tmp));
        tmp[0] = (char)toupper((unsigned char)tmp[0]);
        sym = XStringToKeysym(tmp);
    }
    if (sym == NoSymbol)
        return 0;
    XConvertCase(sym, &lower, &upper);
    return (unsigned long)lower;
}

int ui_main(int argc, char **argv)
{
    Atom wm_delete;
    int i;

    XtSetLanguageProc(NULL, NULL, NULL);
    XtToolkitInitialize();
    app = XtCreateApplicationContext();
    XtAppSetFallbackResources(app, fallbacks);
    dpy = XtOpenDisplay(app, NULL, "qotif", "Qotif", NULL, 0, &argc, argv);
    if (!dpy) {
        fprintf(stderr, "qotif: cannot open display\n");
        return 1;
    }
    /* XtOpenDisplay ran setlocale(LC_ALL, ""); numbers in .map, .cfg and
     * prefs.cfg must always use '.', even under locales such as pt_BR */
    setlocale(LC_NUMERIC, "C");
    if (!choose_visual())
        return 1;
    glctx = glXCreateContext(dpy, glvis, NULL, True);
    if (!glctx) {
        fprintf(stderr, "qotif: cannot create an OpenGL context\n");
        return 1;
    }
    toplevel = XtVaAppCreateShell("qotif", "Qotif", applicationShellWidgetClass, dpy,
                                  XmNvisual, glvis->visual,
                                  XmNdepth, glvis->depth,
                                  XmNcolormap, glcmap,
                                  XmNwidth, 1400, XmNheight, 900,
                                  XmNdeleteResponse, XmDO_NOTHING,
                                  NULL);
    tex_set_free_hook(render_texture_free);
    actions_load_bindings();

    mainwin = XtVaCreateManagedWidget("main", xmMainWindowWidgetClass, toplevel, NULL);
    build_menus();
    workform = XtVaCreateManagedWidget("work", xmFormWidgetClass, mainwin, NULL);
    build_top_bar();
    build_tool_palette();
    build_sidebar();
    build_status_bar();
    build_views();
    layout_workform();
    ui_console_changed();
    apply_layout();
    XtVaSetValues(mainwin, XmNmenuBar, menubar, XmNworkWindow, workform, NULL);
    build_view_menu();
    build_context_menu();
    make_cursors();

    wm_delete = XmInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(toplevel, wm_delete, wm_close_cb, NULL);

    XtRealizeWidget(toplevel);
    log_set_hook(console_log);
    log_info("%s %s - Motif %d.%d.%d (headers), library version %d.%d",
             QOTIF_NAME, QOTIF_VERSION, XmVERSION, XmREVISION, XmUPDATE_LEVEL,
             xmUseVersion / 1000, xmUseVersion % 1000);
    if (xmUseVersion != XmVersion)
        log_warn("the Motif headers used to build Qotif (%d.%d) differ from the libXm "
                 "loaded at run time (%d.%d); remove the extra Motif installation and rebuild",
                 XmVERSION, XmREVISION, xmUseVersion / 1000, xmUseVersion % 1000);
    log_info("configuration: %s%s", config_dir(), paths_portable() ? " (portable)" : "");
    log_info("data directories:");
    for (i = 0; i < data_dir_count(); i++)
        log_info("  %s", data_dir(i));

    ui_sync_toggles();
    ui_tool_changed();
    if (argc > 1 && file_exists(argv[1]))
        ed_load_map(argv[1]);
    else
        ed_new_map();
    ui_entities_changed();
    ui_textures_changed();
    update_selection_info();
    ui_map_changed();
    ui_redraw_all();

    XtAppMainLoop(app);
    return 0;
}
