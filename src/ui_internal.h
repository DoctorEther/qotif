/*
 * ui_internal.h - state shared by the Motif front end modules
 */
#ifndef QOTIF_UI_INTERNAL_H
#define QOTIF_UI_INTERNAL_H

#include <X11/Intrinsic.h>
#include <X11/Xlib.h>
#include <Xm/Xm.h>
#include <GL/glx.h>

/* Motif 2.3.8+ marks deprecated functions with these macros, but some
 * builds only define them for GNU C (not with -std=c99); without them
 * the declarations of XmMessageBoxGetChild() and friends are lost. */
#ifndef XM_DEPRECATED
#define XM_DEPRECATED
#endif
#ifndef XM_ALTERNATIVE
#define XM_ALTERNATIVE(x)
#endif

extern XtAppContext app;
extern Display *dpy;
extern Widget toplevel;
extern XVisualInfo *glvis;
extern GLXContext glctx;
extern Colormap glcmap;

/* appends XmNvisual/XmNdepth/XmNcolormap so popup shells match the GL visual */
Cardinal ui_visual_args(Arg *args, Cardinal n);
XmString xms(const char *s);
void ui_set_label(Widget w, const char *text);
char *ui_text_get(Widget w, char *buf, size_t size);
void ui_text_set(Widget w, const char *text);
int ui_gl_begin(Widget w);
void ui_gl_end(Widget w);
void ui_request_redraw_browser(void);
void ui_request_redraw_preview(void);
Widget ui_form_dialog(const char *name, const char *title);
Widget ui_button(Widget parent, const char *label, XtCallbackProc cb, XtPointer data);
int ui_dir_dialog(const char *title, char *out, size_t size);

/* ui_dialogs.c */
void dlg_selection_changed(void);
void dlg_textures_changed(void);
void dlg_entities_changed(void);
void dlg_redraw_browser(void);
void dlg_refresh_accelerators(void);

/* ui_main.c */
void ui_refresh_accelerators(void);

/* ui_icons.c */
enum { ICON_SELECT, ICON_CAMERA, ICON_ENTITY, ICON_BLOCK, ICON_TEXTURE, ICON_CLIP, ICON_VERTEX, ICON_COUNT };
Pixmap ui_make_icon(Widget w, int icon);

#endif
