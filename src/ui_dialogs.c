/*
 * ui_dialogs.c - Motif dialogs: modal prompts, object properties, face
 * properties, texture browser, preferences, compiling and key help
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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Xm/DrawingA.h>
#include <Xm/FileSB.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/ScrollBar.h>
#include <Xm/SelectioB.h>
#include <Xm/Separator.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

/* ------------------------------------------------------------------ */
/* layout helpers                                                      */

static Widget hrow(Widget parent)
{
    return XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, parent,
                                   XmNorientation, XmHORIZONTAL,
                                   XmNmarginWidth, 0, XmNmarginHeight, 2, NULL);
}

static Widget label(Widget parent, const char *text)
{
    XmString s = xms(text);
    Widget w = XtVaCreateManagedWidget("label", xmLabelWidgetClass, parent,
                                       XmNlabelString, s, XmNalignment, XmALIGNMENT_BEGINNING, NULL);
    XmStringFree(s);
    return w;
}

static Widget text_field(Widget parent, int columns)
{
    return XtVaCreateManagedWidget("field", xmTextFieldWidgetClass, parent, XmNcolumns, columns, NULL);
}

/* "label: [field]" with a fixed label width so rows line up */
static Widget field_row(Widget parent, const char *text, int columns)
{
    Widget row = hrow(parent);
    XmString s = xms(text);
    XtVaCreateManagedWidget("label", xmLabelWidgetClass, row, XmNlabelString, s,
                            XmNalignment, XmALIGNMENT_END, XmNrecomputeSize, False,
                            XmNwidth, 170, NULL);
    XmStringFree(s);
    return text_field(row, columns);
}

static Widget toggle(Widget parent, const char *text, int state)
{
    XmString s = xms(text);
    Widget w = XtVaCreateManagedWidget("toggle", xmToggleButtonWidgetClass, parent,
                                       XmNlabelString, s, XmNset, state ? True : False, NULL);
    XmStringFree(s);
    return w;
}

static Widget scrolled_list(Widget parent, const char *name, int visible, int width)
{
    Arg args[8];
    Cardinal n = 0;
    Widget w;

    XtSetArg(args[n], XmNvisibleItemCount, visible); n++;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNscrollBarDisplayPolicy, XmSTATIC); n++;
    if (width) {
        XtSetArg(args[n], XmNlistSizePolicy, XmCONSTANT); n++;
        XtSetArg(args[n], XmNwidth, width); n++;
    }
    w = XmCreateScrolledList(parent, (char *)name, args, n);
    XtManageChild(w);
    return w;
}

static Widget scrolled_text(Widget parent, const char *name, int rows, int cols, int editable)
{
    Arg args[10];
    Cardinal n = 0;
    Widget w;

    XtSetArg(args[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
    XtSetArg(args[n], XmNrows, rows); n++;
    XtSetArg(args[n], XmNcolumns, cols); n++;
    XtSetArg(args[n], XmNeditable, editable ? True : False); n++;
    XtSetArg(args[n], XmNcursorPositionVisible, editable ? True : False); n++;
    XtSetArg(args[n], XmNwordWrap, editable ? False : True); n++;
    XtSetArg(args[n], XmNscrollHorizontal, editable ? True : False); n++;
    w = XmCreateScrolledText(parent, (char *)name, args, n);
    XtManageChild(w);
    return w;
}

static void list_set_items(Widget list, char **items, int n)
{
    XmString *xs = xmalloc(sizeof(XmString) * (size_t)(n + 1));
    int i;
    for (i = 0; i < n; i++)
        xs[i] = xms(items[i]);
    XmListDeleteAllItems(list);
    if (n)
        XmListAddItems(list, xs, n, 0);
    for (i = 0; i < n; i++)
        XmStringFree(xs[i]);
    free(xs);
}

static void text_append(Widget w, const char *s)
{
    XmTextPosition end = XmTextGetLastPosition(w);
    XmTextInsert(w, end, (char *)s);
    XmTextShowPosition(w, XmTextGetLastPosition(w));
}

static void close_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cb;
    XtUnmanageChild((Widget)cd);
}

static void raise_dialog(Widget d)
{
    view_set_capture(NULL, 0);
    XtManageChild(d);
    if (XtIsRealized(XtParent(d)))
        XRaiseWindow(dpy, XtWindow(XtParent(d)));
}

/* ------------------------------------------------------------------ */
/* modal dialogs                                                       */

typedef struct modal_s {
    int done;
    int result;
} modal_t;

static void m_finish(modal_t *m, int result)
{
    if (!m->done) {
        m->result = result;
        m->done = 1;
    }
}

static void m_ok(Widget w, XtPointer cd, XtPointer cb) { (void)w; (void)cb; m_finish((modal_t *)cd, 1); }
static void m_cancel(Widget w, XtPointer cd, XtPointer cb) { (void)w; (void)cb; m_finish((modal_t *)cd, 0); }
static void m_help(Widget w, XtPointer cd, XtPointer cb) { (void)w; (void)cb; m_finish((modal_t *)cd, 2); }
static void m_unmap(Widget w, XtPointer cd, XtPointer cb) { (void)w; (void)cb; m_finish((modal_t *)cd, 0); }

static void modal_setup(Widget d, modal_t *m)
{
    XtAddCallback(d, XmNokCallback, m_ok, m);
    XtAddCallback(d, XmNcancelCallback, m_cancel, m);
    XtAddCallback(d, XmNhelpCallback, m_help, m);
    XtAddCallback(d, XmNunmapCallback, m_unmap, m);
}

static int modal_run(Widget d, modal_t *m)
{
    view_set_capture(NULL, 0);
    m->done = 0;
    m->result = 0;
    XtManageChild(d);
    while (!m->done)
        XtAppProcessEvent(app, XtIMAll);
    XtUnmanageChild(d);
    return m->result;
}

static Cardinal modal_args(Arg *args)
{
    Cardinal n = ui_visual_args(args, 0);
    XtSetArg(args[n], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL); n++;
    return n;
}

void ui_message(const char *msg)
{
    Arg args[8];
    Cardinal n = modal_args(args);
    XmString s = xms(msg);
    Widget d;
    modal_t m;

    XtSetArg(args[n], XmNmessageString, s); n++;
    d = XmCreateInformationDialog(toplevel, "message", args, n);
    XmStringFree(s);
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_CANCEL_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
    XtVaSetValues(XtParent(d), XmNtitle, QOTIF_NAME, NULL);
    modal_setup(d, &m);
    modal_run(d, &m);
    XtDestroyWidget(XtParent(d));
}

int ui_confirm(const char *msg)
{
    Arg args[8];
    Cardinal n = modal_args(args);
    XmString s = xms(msg);
    Widget d;
    modal_t m;
    int r;

    XtSetArg(args[n], XmNmessageString, s); n++;
    d = XmCreateQuestionDialog(toplevel, "confirm", args, n);
    XmStringFree(s);
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
    XtVaSetValues(XtParent(d), XmNtitle, QOTIF_NAME, NULL);
    modal_setup(d, &m);
    r = modal_run(d, &m);
    XtDestroyWidget(XtParent(d));
    return r == 1;
}

int ui_ask_save(void)
{
    Arg args[12];
    Cardinal n;
    char msg[PATH_LEN + 64];
    XmString s, ok, no, cancel;
    Widget d;
    modal_t m;
    int r;

    if (!ed.map->dirty)
        return 1;
    n = modal_args(args);
    snprintf(msg, sizeof(msg), "Save changes to %s?",
             ed.map->path[0] ? path_basename(ed.map->path) : "the untitled map");
    s = xms(msg);
    ok = xms("Save");
    no = xms("Don't Save");
    cancel = xms("Cancel");
    XtSetArg(args[n], XmNmessageString, s); n++;
    XtSetArg(args[n], XmNokLabelString, ok); n++;
    XtSetArg(args[n], XmNhelpLabelString, no); n++;
    XtSetArg(args[n], XmNcancelLabelString, cancel); n++;
    d = XmCreateWarningDialog(toplevel, "asksave", args, n);
    XmStringFree(s);
    XmStringFree(ok);
    XmStringFree(no);
    XmStringFree(cancel);
    XtVaSetValues(XtParent(d), XmNtitle, QOTIF_NAME, NULL);
    modal_setup(d, &m);
    r = modal_run(d, &m);
    XtDestroyWidget(XtParent(d));
    if (r == 2)
        return 1;
    if (r == 1) {
        action_run_name("save");
        return !ed.map->dirty;
    }
    return 0;
}

int ui_prompt(const char *title, const char *text, char *buf, size_t size)
{
    Arg args[10];
    Cardinal n = modal_args(args);
    XmString l = xms(text), v = xms(buf);
    Widget d;
    modal_t m;
    int r;

    XtSetArg(args[n], XmNselectionLabelString, l); n++;
    XtSetArg(args[n], XmNtextString, v); n++;
    d = XmCreatePromptDialog(toplevel, "prompt", args, n);
    XmStringFree(l);
    XmStringFree(v);
    XtUnmanageChild(XmSelectionBoxGetChild(d, XmDIALOG_HELP_BUTTON));
    XtVaSetValues(XtParent(d), XmNtitle, title, NULL);
    modal_setup(d, &m);
    r = modal_run(d, &m);
    if (r == 1)
        ui_text_get(XmSelectionBoxGetChild(d, XmDIALOG_TEXT), buf, size);
    XtDestroyWidget(XtParent(d));
    return r == 1;
}

static char last_dir[PATH_LEN];

static int file_dialog(const char *title, const char *pattern, int save, int dirs, char *out, size_t size)
{
    Arg args[12];
    Cardinal n = modal_args(args);
    XmString pat, dir;
    Widget d, text;
    modal_t m;
    int ok = 0;

    if (!last_dir[0]) {
        if (ed.map->path[0])
            path_dirname(last_dir, sizeof(last_dir), ed.map->path);
        else if (!getcwd(last_dir, sizeof(last_dir)))
            str_copy(last_dir, "/", sizeof(last_dir));
    }
    pat = xms(pattern);
    dir = xms(last_dir);
    XtSetArg(args[n], XmNpattern, pat); n++;
    XtSetArg(args[n], XmNdirectory, dir); n++;
    if (dirs) {
        XtSetArg(args[n], XmNfileTypeMask, XmFILE_DIRECTORY); n++;
    }
    d = XmCreateFileSelectionDialog(toplevel, "files", args, n);
    XmStringFree(pat);
    XmStringFree(dir);
    XtUnmanageChild(XmFileSelectionBoxGetChild(d, XmDIALOG_HELP_BUTTON));
    XtVaSetValues(XtParent(d), XmNtitle, title, NULL);
    text = XmFileSelectionBoxGetChild(d, XmDIALOG_TEXT);
    if (save && ed.map->path[0])
        ui_text_set(text, ed.map->path);
    modal_setup(d, &m);
    for (;;) {
        char path[PATH_LEN];
        size_t l;

        if (modal_run(d, &m) != 1)
            break;
        ui_text_get(text, path, sizeof(path));
        l = strlen(path);
        if (dirs) {
            while (l > 1 && path[l - 1] == '/')
                path[--l] = 0;
            if (!dir_exists(path)) {
                ui_message("Please choose an existing directory.");
                continue;
            }
            str_copy(out, path, size);
            str_copy(last_dir, path, sizeof(last_dir));
            ok = 1;
            break;
        }
        if (!l || path[l - 1] == '/' || dir_exists(path))
            continue;
        if (!save && !file_exists(path)) {
            ui_message("The file does not exist.");
            continue;
        }
        if (save && file_exists(path)) {
            char msg[PATH_LEN + 64];
            snprintf(msg, sizeof(msg), "%s already exists.\nReplace it?", path);
            if (!ui_confirm(msg))
                continue;
        }
        str_copy(out, path, size);
        path_dirname(last_dir, sizeof(last_dir), path);
        ok = 1;
        break;
    }
    XtDestroyWidget(XtParent(d));
    return ok;
}

int ui_file_dialog(const char *title, const char *pattern, int save, char *out, size_t size)
{
    return file_dialog(title, pattern, save, 0, out, size);
}

int ui_dir_dialog(const char *title, char *out, size_t size)
{
    return file_dialog(title, "*", 0, 1, out, size);
}

static int choose_from_list(const char *title, const char *text, char **items, int nitems,
                            char *out, size_t size)
{
    Arg args[12];
    Cardinal n = modal_args(args);
    XmString *xs = xmalloc(sizeof(XmString) * (size_t)(nitems + 1));
    XmString l = xms(text), v = xms(out);
    Widget d;
    modal_t m;
    int i, r;

    for (i = 0; i < nitems; i++)
        xs[i] = xms(items[i]);
    XtSetArg(args[n], XmNlistItems, xs); n++;
    XtSetArg(args[n], XmNlistItemCount, nitems); n++;
    XtSetArg(args[n], XmNlistVisibleItemCount, 16); n++;
    XtSetArg(args[n], XmNselectionLabelString, l); n++;
    XtSetArg(args[n], XmNtextString, v); n++;
    d = XmCreateSelectionDialog(toplevel, "choose", args, n);
    for (i = 0; i < nitems; i++)
        XmStringFree(xs[i]);
    free(xs);
    XmStringFree(l);
    XmStringFree(v);
    XtUnmanageChild(XmSelectionBoxGetChild(d, XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(d, XmDIALOG_APPLY_BUTTON));
    XtVaSetValues(XtParent(d), XmNtitle, title, NULL);
    modal_setup(d, &m);
    r = modal_run(d, &m);
    if (r == 1)
        ui_text_get(XmSelectionBoxGetChild(d, XmDIALOG_TEXT), out, size);
    XtDestroyWidget(XtParent(d));
    return r == 1 && out[0];
}

/* ------------------------------------------------------------------ */
/* object properties (Hammer's "SmartEdit" sheet)                      */

static struct {
    Widget dlg, class_field, desc, list, key, value, help, choices, flags_rc;
    char **keys;
    int nkeys;
    char cur_key[64];
} ent;

static int ent_targets(entity_t ***out)
{
    int n = sel_entities(out);
    if (!n) {
        *out = xmalloc(sizeof(entity_t *));
        (*out)[0] = ed.map->world;
        n = 1;
    }
    return n;
}

static void ent_free_keys(void)
{
    int i;
    for (i = 0; i < ent.nkeys; i++)
        free(ent.keys[i]);
    free(ent.keys);
    ent.keys = NULL;
    ent.nkeys = 0;
}

static void ent_add_key(const char *k)
{
    ent.keys = xrealloc(ent.keys, sizeof(char *) * (size_t)(ent.nkeys + 1));
    ent.keys[ent.nkeys++] = xstrdup(k);
}

static void ent_show_key(entity_t *e, const char *key)
{
    const eclass_prop_t *p = eclass_find_prop(e->ec, key);
    const char *v = ent_get(e, key);
    char help[1024];
    char **items;
    int i;

    str_copy(ent.cur_key, key, sizeof(ent.cur_key));
    ui_text_set(ent.key, key);
    ui_text_set(ent.value, v ? v : (p ? p->def : ""));
    if (p)
        snprintf(help, sizeof(help), "%s (%s)%s%s", p->desc[0] ? p->desc : p->name, p->type,
                 p->help ? ": " : "", p->help ? p->help : "");
    else
        snprintf(help, sizeof(help), "%s", key);
    ui_set_label(ent.help, help);

    items = xmalloc(sizeof(char *) * (size_t)((p ? p->nchoices : 0) + 1));
    for (i = 0; p && i < p->nchoices && !str_ieq(p->type, "flags"); i++) {
        items[i] = xmalloc(160);
        snprintf(items[i], 160, "%s : %s", p->choices[i].value, p->choices[i].desc);
    }
    list_set_items(ent.choices, items, i);
    while (i--)
        free(items[i]);
    free(items);
}

static void flag_cb(Widget w, XtPointer cd, XtPointer cb);

static void ent_build_flags(entity_t *e)
{
    WidgetList children;
    Cardinal num = 0, i;
    const eclass_prop_t *p = eclass_find_prop(e->ec, "spawnflags");
    const char *sfv = ent_get(e, "spawnflags");
    int sf = sfv ? atoi(sfv) : 0;
    int k;

    XtVaGetValues(ent.flags_rc, XmNchildren, &children, XmNnumChildren, &num, NULL);
    if (num) {
        Widget *copy = xmalloc(sizeof(Widget) * num);
        memcpy(copy, children, sizeof(Widget) * num);
        for (i = 0; i < num; i++)
            XtDestroyWidget(copy[i]);
        free(copy);
    }
    if (!p || !str_ieq(p->type, "flags") || !p->nchoices) {
        label(ent.flags_rc, "(no spawnflags defined for this class)");
        return;
    }
    for (k = 0; k < p->nchoices; k++) {
        int bit = atoi(p->choices[k].value);
        Widget t = toggle(ent.flags_rc, p->choices[k].desc[0] ? p->choices[k].desc : p->choices[k].value,
                          (sf & bit) != 0);
        XtAddCallback(t, XmNvalueChangedCallback, flag_cb, (XtPointer)(long)bit);
    }
}

static void ent_refresh(int rebuild_flags)
{
    entity_t **t, *e;
    int n, i, sel = 0;
    char title[256];
    char **items;
    const eclass_t *ec;

    if (!ent.dlg || !XtIsManaged(ent.dlg))
        return;
    n = ent_targets(&t);
    e = t[0];
    ec = e->ec;
    snprintf(title, sizeof(title), "Object Properties: %s%s", ent_classname(e),
             n > 1 ? " (multiple)" : "");
    XtVaSetValues(XtParent(ent.dlg), XmNtitle, title, NULL);
    ui_text_set(ent.class_field, ent_classname(e));
    ui_text_set(ent.desc, ec && ec->desc ? ec->desc : "No description for this class.");

    ent_free_keys();
    items = xmalloc(sizeof(char *) * (size_t)(e->npairs + (ec ? ec->nprops : 0) + 1));
    for (i = 0; i < e->npairs; i++) {
        items[ent.nkeys] = xmalloc(512);
        snprintf(items[ent.nkeys], 512, "%-22s %s", e->pairs[i].key, e->pairs[i].value);
        ent_add_key(e->pairs[i].key);
    }
    for (i = 0; ec && i < ec->nprops; i++) {
        if (ent_get(e, ec->props[i].name))
            continue;
        items[ent.nkeys] = xmalloc(512);
        snprintf(items[ent.nkeys], 512, "%-22s (%s)", ec->props[i].name,
                 ec->props[i].def[0] ? ec->props[i].def : "unset");
        ent_add_key(ec->props[i].name);
    }
    list_set_items(ent.list, items, ent.nkeys);
    for (i = 0; i < ent.nkeys; i++) {
        free(items[i]);
        if (ent.cur_key[0] && !strcmp(ent.keys[i], ent.cur_key))
            sel = i + 1;
    }
    free(items);
    if (sel) {
        XmListSelectPos(ent.list, sel, False);
        ent_show_key(e, ent.cur_key);
    }
    if (rebuild_flags)
        ent_build_flags(e);
    free(t);
}

static void ent_set_value(const char *key, const char *value)
{
    entity_t **t;
    int n, i, reload = 0;

    if (!key[0])
        return;
    n = ent_targets(&t);
    undo_push("Set Property");
    for (i = 0; i < n; i++) {
        ent_set(t[i], key, value);
        if (!strcmp(key, "classname"))
            eclass_bind(t[i]);
        if (t[i] == ed.map->world && (!strcmp(key, "wad") || !strcmp(key, "_tb_def")))
            reload = 1;
    }
    free(t);
    str_copy(ent.cur_key, key, sizeof(ent.cur_key));
    if (reload)
        ed_load_resources();
    ent_refresh(!strcmp(key, "classname") || !strcmp(key, "spawnflags"));
    ui_redraw_all();
}

static void ent_list_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)cb;
    entity_t **t;
    (void)w;
    (void)cd;
    if (cbs->item_position < 1 || cbs->item_position > ent.nkeys)
        return;
    ent_targets(&t);
    ent_show_key(t[0], ent.keys[cbs->item_position - 1]);
    free(t);
}

static void ent_set_cb(Widget w, XtPointer cd, XtPointer cb)
{
    char key[64], value[1024];
    (void)w;
    (void)cd;
    (void)cb;
    ui_text_get(ent.key, key, sizeof(key));
    ui_text_get(ent.value, value, sizeof(value));
    ent_set_value(str_trim(key), value);
}

static void ent_delete_cb(Widget w, XtPointer cd, XtPointer cb)
{
    char key[64];
    entity_t **t;
    int n, i;
    (void)w;
    (void)cd;
    (void)cb;
    ui_text_get(ent.key, key, sizeof(key));
    if (!key[0] || !strcmp(key, "classname"))
        return;
    n = ent_targets(&t);
    undo_push("Delete Property");
    for (i = 0; i < n; i++)
        ent_remove(t[i], key);
    free(t);
    ent_refresh(!strcmp(key, "spawnflags"));
    ui_redraw_all();
}

static void ent_choice_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)cb;
    entity_t **t;
    const eclass_prop_t *p;
    (void)w;
    (void)cd;
    ent_targets(&t);
    p = eclass_find_prop(t[0]->ec, ent.cur_key);
    free(t);
    if (!p || cbs->item_position < 1 || cbs->item_position > p->nchoices)
        return;
    ui_text_set(ent.value, p->choices[cbs->item_position - 1].value);
    ent_set_value(ent.cur_key, p->choices[cbs->item_position - 1].value);
}

static void flag_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)cb;
    int bit = (int)(long)cd;
    entity_t **t;
    int n, i;
    (void)w;

    n = ent_targets(&t);
    undo_push("Change Flags");
    for (i = 0; i < n; i++) {
        const char *v = ent_get(t[i], "spawnflags");
        int sf = v ? atoi(v) : 0;
        char buf[32];
        sf = cbs->set ? (sf | bit) : (sf & ~bit);
        snprintf(buf, sizeof(buf), "%d", sf);
        if (sf)
            ent_set(t[i], "spawnflags", buf);
        else
            ent_remove(t[i], "spawnflags");
    }
    free(t);
    ent_refresh(0);
}

static void ent_class_cb(Widget w, XtPointer cd, XtPointer cb)
{
    char name[64];
    (void)cd;
    (void)cb;
    (void)w;
    ui_text_get(ent.class_field, name, sizeof(name));
    if (name[0])
        ent_set_value("classname", str_trim(name));
}

static void ent_choose_class_cb(Widget w, XtPointer cd, XtPointer cb)
{
    char **names = NULL, name[64];
    int n = 0, i;
    (void)w;
    (void)cd;
    (void)cb;
    for (i = 0; i < eclass_count(); i++) {
        eclass_t *ec = eclass_get(i);
        if (ec->kind == EC_BASE)
            continue;
        names = xrealloc(names, sizeof(char *) * (size_t)(n + 1));
        names[n++] = ec->name;
    }
    ui_text_get(ent.class_field, name, sizeof(name));
    if (choose_from_list("Entity Class", "Class:", names, n, name, sizeof(name)))
        ent_set_value("classname", str_trim(name));
    free(names);
}

static void build_entity_dialog(void)
{
    Widget top, btns, frame, editrow, sw;
    XmString s;

    ent.dlg = ui_form_dialog("entity", "Object Properties");
    XtVaSetValues(ent.dlg, XmNmarginWidth, 6, XmNmarginHeight, 6, NULL);

    top = hrow(ent.dlg);
    XtVaSetValues(top, XmNtopAttachment, XmATTACH_FORM,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    label(top, "Class:");
    ent.class_field = text_field(top, 28);
    XtAddCallback(ent.class_field, XmNactivateCallback, ent_class_cb, NULL);
    ui_button(top, "Change...", ent_choose_class_cb, NULL);

    ent.desc = scrolled_text(ent.dlg, "desc", 4, 60, 0);
    XtVaSetValues(XtParent(ent.desc), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, top,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);

    btns = hrow(ent.dlg);
    XtVaSetValues(btns, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    ui_button(btns, "Close", close_cb, ent.dlg);

    frame = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, ent.dlg,
                                    XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, btns,
                                    XmNleftAttachment, XmATTACH_FORM,
                                    XmNrightAttachment, XmATTACH_FORM, NULL);
    s = xms("Spawnflags");
    XtVaCreateManagedWidget("title", xmLabelWidgetClass, frame, XmNlabelString, s,
                            XmNchildType, XmFRAME_TITLE_CHILD, NULL);
    XmStringFree(s);
    ent.flags_rc = XtVaCreateManagedWidget("flags", xmRowColumnWidgetClass, frame,
                                           XmNchildType, XmFRAME_WORKAREA_CHILD,
                                           XmNorientation, XmVERTICAL,
                                           XmNpacking, XmPACK_COLUMN,
                                           XmNnumColumns, 3, NULL);

    ent.choices = scrolled_list(ent.dlg, "choices", 4, 0);
    XtAddCallback(ent.choices, XmNbrowseSelectionCallback, ent_choice_cb, NULL);
    sw = XtParent(ent.choices);
    XtVaSetValues(sw, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, frame,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);

    ent.help = label(ent.dlg, " ");
    XtVaSetValues(ent.help, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, sw,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  XmNrecomputeSize, False, NULL);

    editrow = hrow(ent.dlg);
    XtVaSetValues(editrow, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, ent.help,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    label(editrow, "Key:");
    ent.key = text_field(editrow, 16);
    label(editrow, "Value:");
    ent.value = text_field(editrow, 28);
    XtAddCallback(ent.value, XmNactivateCallback, ent_set_cb, NULL);
    ui_button(editrow, "Set", ent_set_cb, NULL);
    ui_button(editrow, "Delete", ent_delete_cb, NULL);

    ent.list = scrolled_list(ent.dlg, "keys", 10, 0);
    XtAddCallback(ent.list, XmNbrowseSelectionCallback, ent_list_cb, NULL);
    XtVaSetValues(XtParent(ent.list), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, XtParent(ent.desc),
                  XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, editrow,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  XmNtopOffset, 4, XmNbottomOffset, 4, NULL);
}

void ui_open_entity_dialog(void)
{
    if (!ent.dlg)
        build_entity_dialog();
    raise_dialog(ent.dlg);
    ent_refresh(1);
}

/* ------------------------------------------------------------------ */
/* face properties                                                     */

static struct {
    Widget dlg, tex, sx, sy, scx, scy, rot, info;
} fc;

static int face_targets(face_t ***out)
{
    entity_t *e;
    brush_t *b;
    face_t **list = NULL;
    int n = 0, i, faces = sel_face_count();

    for (e = ed.map->entities; e; e = e->next)
        for (b = e->brushes; b; b = b->next)
            for (i = 0; i < b->nfaces; i++)
                if (faces ? b->faces[i].selected : b->selected) {
                    list = xrealloc(list, sizeof(face_t *) * (size_t)(n + 1));
                    list[n++] = &b->faces[i];
                }
    *out = list;
    return n;
}

static void set_num(Widget w, double v)
{
    char buf[32];
    fmt_num(buf, sizeof(buf), v);
    ui_text_set(w, buf);
}

static void face_refresh(void)
{
    face_t **f;
    int n;
    char buf[128];

    if (!fc.dlg || !XtIsManaged(fc.dlg))
        return;
    n = face_targets(&f);
    if (n) {
        ui_text_set(fc.tex, f[0]->tex);
        set_num(fc.sx, f[0]->shift[0]);
        set_num(fc.sy, f[0]->shift[1]);
        set_num(fc.scx, f[0]->scale[0]);
        set_num(fc.scy, f[0]->scale[1]);
        set_num(fc.rot, f[0]->rotate);
        snprintf(buf, sizeof(buf), "%d face(s)%s", n, sel_face_count() ? "" : " of the selected brushes");
    } else {
        snprintf(buf, sizeof(buf), "No faces selected.");
    }
    ui_set_label(fc.info, buf);
    free(f);
}

static int read_num(Widget w, double *out)
{
    char buf[64], *end;
    double v;
    ui_text_get(w, buf, sizeof(buf));
    if (!*str_trim(buf))
        return 0;
    v = strtod(buf, &end);
    if (end == buf)
        return 0;
    *out = v;
    return 1;
}

static void face_apply_cb(Widget w, XtPointer cd, XtPointer cb)
{
    face_t **f;
    int n, i;
    char tex[64];
    double sx, sy, scx, scy, rot;
    int hsx, hsy, hscx, hscy, hrot;
    (void)w;
    (void)cd;
    (void)cb;

    n = face_targets(&f);
    if (!n) {
        free(f);
        return;
    }
    ui_text_get(fc.tex, tex, sizeof(tex));
    str_trim(tex);
    hsx = read_num(fc.sx, &sx);
    hsy = read_num(fc.sy, &sy);
    hscx = read_num(fc.scx, &scx) && scx != 0;
    hscy = read_num(fc.scy, &scy) && scy != 0;
    hrot = read_num(fc.rot, &rot);
    undo_push("Face Properties");
    /* face pointers stay valid: undo_push only serializes */
    for (i = 0; i < n; i++) {
        if (tex[0])
            str_copy(f[i]->tex, tex, sizeof(f[i]->tex));
        if (hsx) f[i]->shift[0] = sx;
        if (hsy) f[i]->shift[1] = sy;
        if (hscx) f[i]->scale[0] = scx;
        if (hscy) f[i]->scale[1] = scy;
        if (hrot) {
            if (f[i]->valve && rot != f[i]->rotate) {
                /* rotate the Valve axes around the face normal */
                vec3_t u, v;
                double a = DEG2RAD(rot - f[i]->rotate), c = cos(a), s = sin(a);
                v3_scale(f[i]->uaxis, c, u);
                v3_ma(u, s, f[i]->vaxis, u);
                v3_scale(f[i]->vaxis, c, v);
                v3_ma(v, -s, f[i]->uaxis, v);
                v3_copy(u, f[i]->uaxis);
                v3_copy(v, f[i]->vaxis);
            }
            f[i]->rotate = rot;
        }
    }
    free(f);
    if (tex[0]) {
        str_copy(ed.texture, tex, sizeof(ed.texture));
        ui_textures_changed();
    }
    ui_redraw_all();
}

static void face_fit_cb(Widget w, XtPointer cd, XtPointer cb)
{
    face_t **f;
    int n, i;
    (void)w;
    (void)cd;
    (void)cb;
    n = face_targets(&f);
    if (n) {
        undo_push("Fit Texture");
        for (i = 0; i < n; i++) {
            texture_t *t = tex_find(f[i]->tex);
            face_fit_texture(f[i], t ? t->width : 64, t ? t->height : 64);
        }
    }
    free(f);
    face_refresh();
    ui_redraw_all();
}

static void face_reset_cb(Widget w, XtPointer cd, XtPointer cb)
{
    face_t **f;
    int n, i;
    (void)w;
    (void)cd;
    (void)cb;
    n = face_targets(&f);
    if (n) {
        undo_push("Reset Texture");
        for (i = 0; i < n; i++) {
            char tex[64];
            str_copy(tex, f[i]->tex, sizeof(tex));
            face_init_texture(f[i], tex, f[i]->valve);
        }
    }
    free(f);
    face_refresh();
    ui_redraw_all();
}

static void face_browse_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cd;
    (void)cb;
    ui_open_texture_browser();
}

static void build_face_dialog(void)
{
    Widget rc, row, grid;

    fc.dlg = ui_form_dialog("face", "Face Properties");
    rc = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, fc.dlg,
                                 XmNorientation, XmVERTICAL,
                                 XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
                                 XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                                 XmNmarginWidth, 8, XmNmarginHeight, 8, NULL);
    row = hrow(rc);
    label(row, "Texture:");
    fc.tex = text_field(row, 22);
    XtAddCallback(fc.tex, XmNactivateCallback, face_apply_cb, NULL);
    ui_button(row, "Browse...", face_browse_cb, NULL);

    grid = XtVaCreateManagedWidget("grid", xmRowColumnWidgetClass, rc,
                                   XmNorientation, XmHORIZONTAL, XmNpacking, XmPACK_COLUMN,
                                   XmNnumColumns, 3, XmNisAligned, True,
                                   XmNentryAlignment, XmALIGNMENT_END, NULL);
    label(grid, "Shift X:");
    fc.sx = text_field(grid, 8);
    label(grid, "Shift Y:");
    fc.sy = text_field(grid, 8);
    label(grid, "Scale X:");
    fc.scx = text_field(grid, 8);
    label(grid, "Scale Y:");
    fc.scy = text_field(grid, 8);
    label(grid, "Rotation:");
    fc.rot = text_field(grid, 8);
    label(grid, " ");
    label(grid, " ");

    fc.info = label(rc, "No faces selected.");
    label(rc, "Texture tool, 3D view: LMB selects a face (Ctrl adds, Shift\n"
              "selects the whole brush), RMB applies the current texture,\n"
              "Alt+LMB picks the texture under the cursor.");
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, rc, NULL);
    row = hrow(rc);
    ui_button(row, "Apply", face_apply_cb, NULL);
    ui_button(row, "Fit", face_fit_cb, NULL);
    ui_button(row, "Reset", face_reset_cb, NULL);
    ui_button(row, "Close", close_cb, fc.dlg);
}

void ui_open_face_dialog(void)
{
    if (!fc.dlg)
        build_face_dialog();
    raise_dialog(fc.dlg);
    face_refresh();
}

/* ------------------------------------------------------------------ */
/* texture browser                                                     */

static struct {
    Widget dlg, filter, area, sb, info;
    texbrowser_t tb;
    Time last_time;
    int last_hit;
} tbw;

static void tb_update_info(void)
{
    char buf[160];
    texture_t *t = tex_find(ed.texture);
    if (t)
        snprintf(buf, sizeof(buf), "%s  %dx%d  (%s)", t->name, t->width, t->height, t->source);
    else
        snprintf(buf, sizeof(buf), "%d textures", tex_count());
    ui_set_label(tbw.info, buf);
}

void dlg_redraw_browser(void)
{
    Dimension w, h;
    int max, slider, value;

    if (!tbw.dlg || !XtIsManaged(tbw.dlg) || !ui_gl_begin(tbw.area))
        return;
    XtVaGetValues(tbw.area, XmNwidth, &w, XmNheight, &h, NULL);
    tbw.tb.width = w;
    tbw.tb.height = h;
    texbrowser_render(&tbw.tb);
    ui_gl_end(tbw.area);

    max = tbw.tb.content_height > h ? tbw.tb.content_height : h;
    if (max < 1)
        max = 1;
    slider = h < max ? h : max;
    if (slider < 1)
        slider = 1;
    value = tbw.tb.scroll;
    if (value > max - slider)
        value = max - slider;
    if (value < 0)
        value = 0;
    XtVaSetValues(tbw.sb, XmNminimum, 0, XmNmaximum, max, XmNsliderSize, slider,
                  XmNvalue, value, XmNpageIncrement, h > 32 ? h - 32 : 1, XmNincrement, 32, NULL);
    if (value != tbw.tb.scroll) {
        tbw.tb.scroll = value;
        ui_request_redraw_browser();
    }
}

static void tb_expose_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cd;
    (void)cb;
    ui_request_redraw_browser();
}

static void tb_scroll_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmScrollBarCallbackStruct *cbs = (XmScrollBarCallbackStruct *)cb;
    (void)w;
    (void)cd;
    tbw.tb.scroll = cbs->value;
    ui_request_redraw_browser();
}

static void tb_event(Widget w, XtPointer cd, XEvent *ev, Boolean *cont)
{
    (void)w;
    (void)cd;
    (void)cont;
    if (ev->type != ButtonPress)
        return;
    if (ev->xbutton.button == Button4 || ev->xbutton.button == Button5) {
        tbw.tb.scroll += ev->xbutton.button == Button4 ? -64 : 64;
        if (tbw.tb.scroll < 0)
            tbw.tb.scroll = 0;
        ui_request_redraw_browser();
        return;
    }
    if (ev->xbutton.button == Button1) {
        int hit = texbrowser_hit(&tbw.tb, ev->xbutton.x, ev->xbutton.y);
        int dbl = hit >= 0 && hit == tbw.last_hit
               && ev->xbutton.time - tbw.last_time < (Time)XtGetMultiClickTime(dpy);
        tbw.last_hit = hit;
        tbw.last_time = ev->xbutton.time;
        if (hit < 0)
            return;
        str_copy(ed.texture, tex_get(hit)->name, sizeof(ed.texture));
        ui_textures_changed();
        if (dbl)
            cmd_apply_texture();
    }
}

static void tb_filter_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)cd;
    (void)cb;
    ui_text_get(w, tbw.tb.filter, sizeof(tbw.tb.filter));
    tbw.tb.scroll = 0;
    ui_request_redraw_browser();
}

static void tb_used_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)cb;
    (void)w;
    (void)cd;
    tbw.tb.used_only = cbs->set;
    texbrowser_update_used();
    tbw.tb.scroll = 0;
    ui_request_redraw_browser();
}

static void tb_size_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cb;
    tbw.tb.thumb = (int)(long)cd;
    prefs.thumb_size = tbw.tb.thumb;
    ui_request_redraw_browser();
}

static void tb_apply_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cd;
    (void)cb;
    cmd_apply_texture();
}

static void build_texture_browser(void)
{
    Widget top, bottom;

    tbw.dlg = ui_form_dialog("textures", "Texture Browser");
    tbw.tb.thumb = prefs.thumb_size > 16 ? prefs.thumb_size : 128;
    tbw.last_hit = -1;

    top = hrow(tbw.dlg);
    XtVaSetValues(top, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    label(top, "Filter:");
    tbw.filter = text_field(top, 20);
    XtAddCallback(tbw.filter, XmNvalueChangedCallback, tb_filter_cb, NULL);
    XtAddCallback(toggle(top, "Used in map", 0), XmNvalueChangedCallback, tb_used_cb, NULL);
    label(top, "   Size:");
    ui_button(top, "64", tb_size_cb, (XtPointer)64L);
    ui_button(top, "128", tb_size_cb, (XtPointer)128L);
    ui_button(top, "256", tb_size_cb, (XtPointer)256L);

    bottom = hrow(tbw.dlg);
    XtVaSetValues(bottom, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    ui_button(bottom, "Apply to Selection", tb_apply_cb, NULL);
    ui_button(bottom, "Close", close_cb, tbw.dlg);
    tbw.info = label(bottom, " ");

    tbw.sb = XtVaCreateManagedWidget("scroll", xmScrollBarWidgetClass, tbw.dlg,
                                     XmNorientation, XmVERTICAL,
                                     XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, top,
                                     XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, bottom,
                                     XmNrightAttachment, XmATTACH_FORM, NULL);
    XtAddCallback(tbw.sb, XmNvalueChangedCallback, tb_scroll_cb, NULL);
    XtAddCallback(tbw.sb, XmNdragCallback, tb_scroll_cb, NULL);

    tbw.area = XtVaCreateManagedWidget("browser", xmDrawingAreaWidgetClass, tbw.dlg,
                                       XmNwidth, 720, XmNheight, 520,
                                       XmNresizePolicy, XmRESIZE_NONE,
                                       XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, top,
                                       XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, bottom,
                                       XmNleftAttachment, XmATTACH_FORM,
                                       XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, tbw.sb,
                                       NULL);
    XtAddCallback(tbw.area, XmNexposeCallback, tb_expose_cb, NULL);
    XtAddCallback(tbw.area, XmNresizeCallback, tb_expose_cb, NULL);
    XtAddEventHandler(tbw.area, ButtonPressMask, False, tb_event, NULL);
}

void ui_open_texture_browser(void)
{
    int y;
    if (!tbw.dlg)
        build_texture_browser();
    texbrowser_update_used();
    raise_dialog(tbw.dlg);
    y = texbrowser_find(&tbw.tb, ed.texture);
    if (y >= 0)
        tbw.tb.scroll = y > 24 ? y - 24 : 0;
    tb_update_info();
    ui_request_redraw_browser();
}

/* ------------------------------------------------------------------ */
/* preferences                                                         */

enum { PAGE_GAMES, PAGE_VIEW, PAGE_COLORS, PAGE_KEYS, PAGE_COUNT };

static struct {
    Widget dlg, pages[PAGE_COUNT];
    Widget game_list, game_path, game_mod, game_vars, game_info;
    Widget fov, sens, fly, gmajor, undo, thumb, font, invert, linear, mipmaps, names, links;
    Widget color_list, color_field;
    Widget key_list, key_field, key_info;
    int game_sel, color_sel, key_sel;
    float color_backup[16][3];
} pr;

static void show_page(int page)
{
    int i;
    for (i = 0; i < PAGE_COUNT; i++) {
        if (i == page)
            XtManageChild(pr.pages[i]);
        else
            XtUnmanageChild(pr.pages[i]);
    }
}

static void tab_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)cb;
    (void)w;
    if (cbs->set)
        show_page((int)(long)cd);
}

static void game_load_fields(void)
{
    game_t *g;
    strbuf_t sb;
    char section[128], info[PATH_LEN + 256];
    int i;

    if (pr.game_sel < 0 || pr.game_sel >= ngames)
        return;
    g = games[pr.game_sel];
    ui_text_set(pr.game_path, prefs_game_get(g->name, "path", ""));
    ui_text_set(pr.game_mod, prefs_game_get(g->name, "mod", ""));
    sb_init(&sb);
    snprintf(section, sizeof(section), "game %s", g->name);
    for (i = ini_next(&prefs.ini, 0, section, NULL); i >= 0; i = ini_next(&prefs.ini, i + 1, section, NULL)) {
        if (str_ieq(prefs.ini.e[i].key, "path") || str_ieq(prefs.ini.e[i].key, "mod"))
            continue;
        sb_appendf(&sb, "%s = %s\n", prefs.ini.e[i].key, prefs.ini.e[i].value);
    }
    for (i = ini_next(&g->ini, 0, "variables", NULL); i >= 0; i = ini_next(&g->ini, i + 1, "variables", NULL))
        if (!prefs_game_get(g->name, g->ini.e[i].key, NULL))
            sb_appendf(&sb, "# %s = %s   (default)\n", g->ini.e[i].key, g->ini.e[i].value);
    ui_text_set(pr.game_vars, sb.data ? sb.data : "");
    sb_free(&sb);
    snprintf(info, sizeof(info), "Configuration: %s\nBase directory: %s   Format: %s   Profiles: %d",
             g->cfgpath, g->basedir, g->valve220 ? "Valve 220" : "Standard", g->nprofiles);
    ui_set_label(pr.game_info, info);
}

static void game_save_fields(void)
{
    game_t *g;
    char section[128], path[PATH_LEN], mod[128];
    char *text, *line, *next;

    if (pr.game_sel < 0 || pr.game_sel >= ngames)
        return;
    g = games[pr.game_sel];
    ui_text_get(pr.game_path, path, sizeof(path));
    ui_text_get(pr.game_mod, mod, sizeof(mod));
    snprintf(section, sizeof(section), "game %s", g->name);
    ini_remove_section(&prefs.ini, section);
    prefs_game_set(g->name, "path", str_trim(path));
    prefs_game_set(g->name, "mod", str_trim(mod));
    text = XmTextGetString(pr.game_vars);
    for (line = text; line && *line; line = next) {
        char *eq, *s;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        s = str_trim(line);
        if (!*s || *s == '#' || *s == ';')
            continue;
        eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = 0;
        if (*str_trim(s))
            prefs_game_set(g->name, str_trim(s), str_trim(eq + 1));
    }
    XtFree(text);
}

static void game_list_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)cb;
    (void)w;
    (void)cd;
    game_save_fields();
    pr.game_sel = cbs->item_position - 1;
    game_load_fields();
}

static void game_browse_cb(Widget w, XtPointer cd, XtPointer cb)
{
    char dir[PATH_LEN];
    (void)w;
    (void)cd;
    (void)cb;
    if (ui_dir_dialog("Game Directory", dir, sizeof(dir)))
        ui_text_set(pr.game_path, dir);
}

static void color_list_refresh(void)
{
    char **items = xmalloc(sizeof(char *) * (size_t)num_pref_colors);
    int i;
    for (i = 0; i < num_pref_colors; i++) {
        const float *c = pref_colors[i].rgb;
        items[i] = xmalloc(128);
        snprintf(items[i], 128, "%-22s %.2f %.2f %.2f", pref_colors[i].label, c[0], c[1], c[2]);
    }
    list_set_items(pr.color_list, items, num_pref_colors);
    for (i = 0; i < num_pref_colors; i++)
        free(items[i]);
    free(items);
    if (pr.color_sel >= 0)
        XmListSelectPos(pr.color_list, pr.color_sel + 1, False);
}

static void color_list_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)cb;
    char buf[64];
    const float *c;
    (void)w;
    (void)cd;
    pr.color_sel = cbs->item_position - 1;
    c = pref_colors[pr.color_sel].rgb;
    snprintf(buf, sizeof(buf), "%.3f %.3f %.3f", c[0], c[1], c[2]);
    ui_text_set(pr.color_field, buf);
}

static void color_set_cb(Widget w, XtPointer cd, XtPointer cb)
{
    char buf[64];
    float r, g, b;
    (void)w;
    (void)cd;
    (void)cb;
    if (pr.color_sel < 0)
        return;
    ui_text_get(pr.color_field, buf, sizeof(buf));
    if (sscanf(buf, "%f %f %f", &r, &g, &b) != 3)
        return;
    pref_colors[pr.color_sel].rgb[0] = r;
    pref_colors[pr.color_sel].rgb[1] = g;
    pref_colors[pr.color_sel].rgb[2] = b;
    color_list_refresh();
    ui_redraw_all();
}

static void key_item(const action_t *a, char *buf, size_t size)
{
    char keys[128];
    keybind_format(a, keys, sizeof(keys));
    snprintf(buf, size, "%-34s %s", a->label, keys);
}

static void key_list_refresh(void)
{
    char **items = xmalloc(sizeof(char *) * (size_t)num_actions);
    int i;
    for (i = 0; i < num_actions; i++) {
        items[i] = xmalloc(192);
        key_item(&actions[i], items[i], 192);
    }
    list_set_items(pr.key_list, items, num_actions);
    for (i = 0; i < num_actions; i++)
        free(items[i]);
    free(items);
    if (pr.key_sel >= 0) {
        XmListSelectPos(pr.key_list, pr.key_sel + 1, False);
        XmListSetBottomPos(pr.key_list, pr.key_sel + 1);
    }
}

static void key_list_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)cb;
    char buf[128];
    (void)w;
    (void)cd;
    pr.key_sel = cbs->item_position - 1;
    keybind_format(&actions[pr.key_sel], buf, sizeof(buf));
    ui_text_set(pr.key_field, buf);
    ui_set_label(pr.key_info, actions[pr.key_sel].name);
}

static void key_change(int mode)
{
    action_t *a;
    char buf[128], msg[256];
    int i, k, j;

    if (pr.key_sel < 0)
        return;
    a = &actions[pr.key_sel];
    if (mode == 0) {
        ui_text_get(pr.key_field, buf, sizeof(buf));
        a->nkeys = keybind_parse(buf, a->keys, MAX_BINDINGS);
    } else if (mode == 1) {
        a->nkeys = 0;
    } else {
        actions_reset_binding(a);
    }
    keybind_format(a, buf, sizeof(buf));
    ui_text_set(pr.key_field, buf);
    snprintf(msg, sizeof(msg), "%s", a->name);
    /* report conflicts */
    for (i = 0; i < num_actions; i++) {
        if (&actions[i] == a)
            continue;
        for (k = 0; k < actions[i].nkeys; k++)
            for (j = 0; j < a->nkeys; j++)
                if (actions[i].keys[k].sym == a->keys[j].sym && actions[i].keys[k].mods == a->keys[j].mods
                    && !actions[i].fly == !a->fly)
                    snprintf(msg, sizeof(msg), "%s - conflicts with \"%s\"", a->name, actions[i].label);
    }
    ui_set_label(pr.key_info, msg);
    key_list_refresh();
}

static void key_set_cb(Widget w, XtPointer cd, XtPointer cb) { (void)w; (void)cb; key_change((int)(long)cd); }

static void prefs_fill(void)
{
    char buf[64];
    int i;

    for (i = 0; i < ngames; i++)
        if (str_ieq(games[i]->name, prefs.game))
            pr.game_sel = i;
    {
        char **items = xmalloc(sizeof(char *) * (size_t)(ngames + 1));
        for (i = 0; i < ngames; i++)
            items[i] = games[i]->name;
        list_set_items(pr.game_list, items, ngames);
        free(items);
    }
    if (pr.game_sel >= 0 && pr.game_sel < ngames)
        XmListSelectPos(pr.game_list, pr.game_sel + 1, False);
    game_load_fields();

    snprintf(buf, sizeof(buf), "%g", prefs.fov);
    ui_text_set(pr.fov, buf);
    snprintf(buf, sizeof(buf), "%g", prefs.sensitivity);
    ui_text_set(pr.sens, buf);
    snprintf(buf, sizeof(buf), "%g", prefs.flyspeed);
    ui_text_set(pr.fly, buf);
    snprintf(buf, sizeof(buf), "%d", prefs.grid_major);
    ui_text_set(pr.gmajor, buf);
    snprintf(buf, sizeof(buf), "%d", prefs.undo_levels);
    ui_text_set(pr.undo, buf);
    snprintf(buf, sizeof(buf), "%d", prefs.thumb_size);
    ui_text_set(pr.thumb, buf);
    ui_text_set(pr.font, prefs.font);
    XmToggleButtonSetState(pr.invert, prefs.invert_mouse ? True : False, False);
    XmToggleButtonSetState(pr.linear, prefs.tex_linear ? True : False, False);
    XmToggleButtonSetState(pr.mipmaps, prefs.tex_mipmaps ? True : False, False);
    XmToggleButtonSetState(pr.names, prefs.show_names ? True : False, False);
    XmToggleButtonSetState(pr.links, prefs.show_links ? True : False, False);

    for (i = 0; i < num_pref_colors && i < 16; i++)
        memcpy(pr.color_backup[i], pref_colors[i].rgb, sizeof(float) * 3);
    color_list_refresh();
    key_list_refresh();
}

static double field_double(Widget w, double def)
{
    double v;
    return read_num(w, &v) ? v : def;
}

static void prefs_apply(void)
{
    char oldgame[64], oldpath[PATH_LEN], oldmod[128];
    int oldlinear = prefs.tex_linear, oldmipmaps = prefs.tex_mipmaps;

    str_copy(oldgame, prefs.game, sizeof(oldgame));
    str_copy(oldpath, prefs_game_get(prefs.game, "path", ""), sizeof(oldpath));
    str_copy(oldmod, prefs_game_get(prefs.game, "mod", ""), sizeof(oldmod));

    game_save_fields();
    if (pr.game_sel >= 0 && pr.game_sel < ngames)
        str_copy(prefs.game, games[pr.game_sel]->name, sizeof(prefs.game));

    prefs.fov = field_double(pr.fov, prefs.fov);
    if (prefs.fov < 20 || prefs.fov > 150)
        prefs.fov = 75;
    prefs.sensitivity = field_double(pr.sens, prefs.sensitivity);
    prefs.flyspeed = field_double(pr.fly, prefs.flyspeed);
    prefs.grid_major = (int)field_double(pr.gmajor, prefs.grid_major);
    if (prefs.grid_major < 2)
        prefs.grid_major = 64;
    prefs.undo_levels = (int)field_double(pr.undo, prefs.undo_levels);
    if (prefs.undo_levels < 1)
        prefs.undo_levels = 1;
    prefs.thumb_size = (int)field_double(pr.thumb, prefs.thumb_size);
    if (prefs.thumb_size < 32)
        prefs.thumb_size = 32;
    ui_text_get(pr.font, prefs.font, sizeof(prefs.font));
    prefs.invert_mouse = XmToggleButtonGetState(pr.invert);
    prefs.tex_linear = XmToggleButtonGetState(pr.linear);
    prefs.tex_mipmaps = XmToggleButtonGetState(pr.mipmaps);
    prefs.show_names = XmToggleButtonGetState(pr.names);
    prefs.show_links = XmToggleButtonGetState(pr.links);
    tbw.tb.thumb = prefs.thumb_size;

    actions_save_bindings();
    prefs_save();
    ui_refresh_accelerators();
    ui_sync_toggles();
    if (!str_ieq(oldgame, prefs.game) || strcmp(oldpath, prefs_game_get(prefs.game, "path", ""))
        || strcmp(oldmod, prefs_game_get(prefs.game, "mod", ""))) {
        log_info("game set to %s", prefs.game);
        ed_load_resources();
        ui_map_changed();
    } else if ((oldlinear != prefs.tex_linear || oldmipmaps != prefs.tex_mipmaps)
               && ui_gl_begin((Widget)views[0].ui)) {
        render_reset_textures();
    }
    ui_redraw_all();
}

static void prefs_ok_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cd;
    (void)cb;
    prefs_apply();
    XtUnmanageChild(pr.dlg);
}

static void prefs_apply_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cd;
    (void)cb;
    prefs_apply();
}

static void prefs_cancel_cb(Widget w, XtPointer cd, XtPointer cb)
{
    int i;
    (void)w;
    (void)cd;
    (void)cb;
    for (i = 0; i < num_pref_colors && i < 16; i++)
        memcpy(pref_colors[i].rgb, pr.color_backup[i], sizeof(float) * 3);
    actions_load_bindings();
    XtUnmanageChild(pr.dlg);
    ui_redraw_all();
}

static Widget make_page(Widget parent, Widget top, Widget bottom)
{
    return XtVaCreateWidget("page", xmRowColumnWidgetClass, parent,
                            XmNorientation, XmVERTICAL,
                            XmNmarginWidth, 8, XmNmarginHeight, 8,
                            XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, top,
                            XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, bottom,
                            XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                            NULL);
}

static void build_prefs_dialog(void)
{
    static const char *tabs[PAGE_COUNT] = { "Games", "View", "Colors", "Keyboard" };
    Widget tabrow, btns, p, row;
    int i;

    pr.dlg = ui_form_dialog("prefs", "Preferences");
    pr.game_sel = pr.color_sel = pr.key_sel = -1;

    tabrow = XtVaCreateManagedWidget("tabs", xmRowColumnWidgetClass, pr.dlg,
                                     XmNorientation, XmHORIZONTAL,
                                     XmNradioBehavior, True, XmNradioAlwaysOne, True,
                                     XmNtopAttachment, XmATTACH_FORM,
                                     XmNleftAttachment, XmATTACH_FORM, NULL);
    for (i = 0; i < PAGE_COUNT; i++) {
        Widget t = toggle(tabrow, tabs[i], i == 0);
        XtVaSetValues(t, XmNindicatorOn, False, XmNshadowThickness, 2, XmNfillOnSelect, True, NULL);
        XtAddCallback(t, XmNvalueChangedCallback, tab_cb, (XtPointer)(long)i);
    }
    btns = hrow(pr.dlg);
    XtVaSetValues(btns, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    ui_button(btns, "OK", prefs_ok_cb, NULL);
    ui_button(btns, "Apply", prefs_apply_cb, NULL);
    ui_button(btns, "Cancel", prefs_cancel_cb, NULL);

    /* games */
    p = pr.pages[PAGE_GAMES] = make_page(pr.dlg, tabrow, btns);
    label(p, "Game configuration (games/*.cfg in the data directories):");
    pr.game_list = scrolled_list(p, "games", 5, 420);
    XtAddCallback(pr.game_list, XmNbrowseSelectionCallback, game_list_cb, NULL);
    label(p, "Game path (the directory that contains id1/, valve/, ...):");
    row = hrow(p);
    pr.game_path = text_field(row, 44);
    ui_button(row, "Browse...", game_browse_cb, NULL);
    label(p, "Mod directory (optional, searched before the base directory):");
    pr.game_mod = text_field(p, 20);
    label(p, "Compile tool variables, one \"NAME = value\" per line (used as ${NAME}):");
    pr.game_vars = scrolled_text(p, "vars", 7, 60, 1);
    pr.game_info = label(p, " ");

    /* view */
    p = pr.pages[PAGE_VIEW] = make_page(pr.dlg, tabrow, btns);
    pr.fov = field_row(p, "3D field of view:", 8);
    pr.sens = field_row(p, "Mouse look sensitivity:", 8);
    pr.fly = field_row(p, "Fly speed (units/s):", 8);
    pr.gmajor = field_row(p, "Major grid lines every:", 8);
    pr.undo = field_row(p, "Undo levels:", 8);
    pr.thumb = field_row(p, "Texture thumbnail size:", 8);
    pr.font = field_row(p, "View font (X font name):", 40);
    pr.invert = toggle(p, "Invert mouse look", 0);
    pr.linear = toggle(p, "Linear texture filtering", 0);
    pr.mipmaps = toggle(p, "Texture mipmapping", 1);
    pr.names = toggle(p, "Show entity names in 2D views", 1);
    pr.links = toggle(p, "Show entity target links", 1);
    label(p, "Font changes take effect after a restart.");

    /* colors */
    p = pr.pages[PAGE_COLORS] = make_page(pr.dlg, tabrow, btns);
    label(p, "Colors (red green blue, 0 to 1):");
    pr.color_list = scrolled_list(p, "colors", 12, 420);
    XtAddCallback(pr.color_list, XmNbrowseSelectionCallback, color_list_cb, NULL);
    row = hrow(p);
    pr.color_field = text_field(row, 20);
    XtAddCallback(pr.color_field, XmNactivateCallback, color_set_cb, NULL);
    ui_button(row, "Set", color_set_cb, NULL);

    /* keyboard */
    p = pr.pages[PAGE_KEYS] = make_page(pr.dlg, tabrow, btns);
    label(p, "Key bindings, e.g. \"Ctrl+Shift+S\" or \"bracketleft\"; two may be given\n"
             "separated by a comma. Key names are X keysym names.");
    pr.key_list = scrolled_list(p, "keys", 16, 460);
    XtAddCallback(pr.key_list, XmNbrowseSelectionCallback, key_list_cb, NULL);
    row = hrow(p);
    pr.key_field = text_field(row, 28);
    XtAddCallback(pr.key_field, XmNactivateCallback, key_set_cb, (XtPointer)0L);
    ui_button(row, "Set", key_set_cb, (XtPointer)0L);
    ui_button(row, "Clear", key_set_cb, (XtPointer)1L);
    ui_button(row, "Default", key_set_cb, (XtPointer)2L);
    pr.key_info = label(p, " ");
    XtVaSetValues(pr.key_info, XmNrecomputeSize, True, NULL);

    show_page(PAGE_GAMES);
}

void ui_open_prefs_dialog(void)
{
    if (!pr.dlg)
        build_prefs_dialog();
    prefs_fill();
    raise_dialog(pr.dlg);
}

/* ------------------------------------------------------------------ */
/* compile / run                                                       */

static struct {
    Widget dlg, plist, cmds, out;
    int prof;
    char **steps;
    int nsteps, step;
    pid_t pid;
    int fd;
    XtInputId input;
} cp;

static game_t *cp_game(void)
{
    return game_current();
}

static void cp_free_steps(void)
{
    int i;
    for (i = 0; i < cp.nsteps; i++)
        free(cp.steps[i]);
    free(cp.steps);
    cp.steps = NULL;
    cp.nsteps = 0;
}

static void cp_expand(void)
{
    game_t *g = cp_game();
    const profile_t *p;
    strbuf_t sb;
    int i;

    cp_free_steps();
    if (!g || cp.prof < 0 || cp.prof >= g->nprofiles)
        return;
    p = &g->profiles[cp.prof];
    sb_init(&sb);
    for (i = 0; i < p->ncmds; i++) {
        char cmd[4096];
        game_expand(g, ed.map->path[0] ? ed.map->path : "untitled.map", p->cmds[i], cmd, sizeof(cmd));
        cp.steps = xrealloc(cp.steps, sizeof(char *) * (size_t)(cp.nsteps + 1));
        cp.steps[cp.nsteps++] = xstrdup(cmd);
        sb_appendf(&sb, "%d. %s\n", i + 1, cmd);
    }
    ui_text_set(cp.cmds, sb.data ? sb.data : "");
    sb_free(&sb);
}

static void cp_fill(void)
{
    game_t *g = cp_game();
    char **items;
    int i;

    if (!g)
        return;
    items = xmalloc(sizeof(char *) * (size_t)(g->nprofiles + 1));
    for (i = 0; i < g->nprofiles; i++)
        items[i] = g->profiles[i].name;
    list_set_items(cp.plist, items, g->nprofiles);
    free(items);
    if (cp.prof < 0 || cp.prof >= g->nprofiles)
        cp.prof = 0;
    if (g->nprofiles)
        XmListSelectPos(cp.plist, cp.prof + 1, False);
    else
        ui_text_set(cp.cmds, "No compile profiles. Add [profile <name>] sections with\n"
                             "\"cmd = ...\" lines to the game configuration file.");
    cp_expand();
}

static void cp_start_step(void);

static void cp_input_cb(XtPointer cd, int *source, XtInputId *id)
{
    char buf[4097];
    ssize_t r;
    (void)cd;

    r = read(*source, buf, sizeof(buf) - 1);
    if (r > 0) {
        ssize_t i;
        for (i = 0; i < r; i++)
            if (!buf[i])
                buf[i] = ' ';
        buf[r] = 0;
        text_append(cp.out, buf);
        return;
    }
    if (r < 0 && (errno == EINTR || errno == EAGAIN))
        return;
    XtRemoveInput(*id);
    close(*source);
    cp.input = 0;
    cp.fd = -1;
    {
        int status = 0;
        char msg[128];
        waitpid(cp.pid, &status, 0);
        cp.pid = 0;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            cp.step++;
            cp_start_step();
            return;
        }
        if (WIFEXITED(status))
            snprintf(msg, sizeof(msg), "\n*** step %d failed (exit status %d)\n", cp.step + 1, WEXITSTATUS(status));
        else
            snprintf(msg, sizeof(msg), "\n*** step %d terminated\n", cp.step + 1);
        text_append(cp.out, msg);
        log_warn("compile: step %d failed", cp.step + 1);
    }
}

static void cp_start_step(void)
{
    int fds[2];
    char line[4200], dir[PATH_LEN];
    pid_t pid;

    if (cp.step >= cp.nsteps) {
        text_append(cp.out, "\n*** done\n");
        log_info("compile finished");
        return;
    }
    snprintf(line, sizeof(line), "\n> %s\n", cp.steps[cp.step]);
    text_append(cp.out, line);
    if (pipe(fds) != 0) {
        text_append(cp.out, "*** pipe() failed\n");
        return;
    }
    path_dirname(dir, sizeof(dir), ed.map->path);
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        text_append(cp.out, "*** fork() failed\n");
        return;
    }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        if (chdir(dir) != 0)
            _exit(126);
        execl("/bin/sh", "sh", "-c", cp.steps[cp.step], (char *)NULL);
        _exit(127);
    }
    setpgid(pid, pid);
    close(fds[1]);
    cp.pid = pid;
    cp.fd = fds[0];
    cp.input = XtAppAddInput(app, cp.fd, (XtPointer)XtInputReadMask, cp_input_cb, NULL);
}

static void cp_run_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cd;
    (void)cb;
    if (cp.pid)
        return;
    if (!ed.map->path[0] || ed.map->dirty) {
        action_run_name("save");
        if (!ed.map->path[0] || ed.map->dirty)
            return;
    }
    cp_expand();
    if (!cp.nsteps)
        return;
    ui_text_set(cp.out, "");
    cp.step = 0;
    cp_start_step();
}

static void cp_stop_cb(Widget w, XtPointer cd, XtPointer cb)
{
    (void)w;
    (void)cd;
    (void)cb;
    if (cp.pid > 0) {
        kill(-cp.pid, SIGTERM);
        cp.nsteps = cp.step;    /* do not continue with later steps */
    }
}

static void cp_list_cb(Widget w, XtPointer cd, XtPointer cb)
{
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)cb;
    (void)w;
    (void)cd;
    cp.prof = cbs->item_position - 1;
    cp_expand();
}

static void build_compile_dialog(void)
{
    Widget top, btns;

    cp.dlg = ui_form_dialog("compile", "Compile / Run Map");
    cp.fd = -1;
    top = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, cp.dlg,
                                  XmNorientation, XmVERTICAL,
                                  XmNtopAttachment, XmATTACH_FORM,
                                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                                  NULL);
    label(top, "Profile:");
    cp.plist = scrolled_list(top, "profiles", 4, 560);
    XtAddCallback(cp.plist, XmNbrowseSelectionCallback, cp_list_cb, NULL);
    label(top, "Commands (run in the map's directory; variables from Preferences > Games):");
    cp.cmds = scrolled_text(top, "cmds", 5, 80, 0);

    btns = hrow(cp.dlg);
    XtVaSetValues(btns, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    ui_button(btns, "Run", cp_run_cb, NULL);
    ui_button(btns, "Stop", cp_stop_cb, NULL);
    ui_button(btns, "Close", close_cb, cp.dlg);

    cp.out = scrolled_text(cp.dlg, "output", 16, 80, 0);
    XtVaSetValues(XtParent(cp.out),
                  XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, top,
                  XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, btns,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
}

void ui_open_compile_dialog(void)
{
    if (!cp.dlg)
        build_compile_dialog();
    cp_fill();
    raise_dialog(cp.dlg);
}

/* ------------------------------------------------------------------ */
/* keyboard help                                                       */

static Widget keys_dlg, keys_text;

static void keys_fill(void)
{
    strbuf_t sb;
    int i;

    if (!keys_text)
        return;
    sb_init(&sb);
    sb_append(&sb,
              "MOUSE\n"
              "  2D views:  LMB  current tool (select, drag to move, handles scale/rotate;\n"
              "                  click the selection again to switch handle mode)\n"
              "             Shift+drag clones, Ctrl+click adds/removes, drag on empty space\n"
              "             selects by rectangle.  MMB/RMB drag pans, wheel zooms,\n"
              "             RMB click opens the context menu.\n"
              "  3D view:   LMB  select (texture tool: pick faces), RMB drag mouse-look\n"
              "             (WASD/QE fly while held, Shift = faster), MMB drag strafes,\n"
              "             wheel moves forward.  Z captures the mouse (look without a\n"
              "             button held, WASD/QE fly; Z or Escape releases it).\n"
              "  Shift+LMB in 3D selects single faces (Ctrl+Shift adds) with any tool.\n"
              "  Vertex tool: drag the white (vertex), blue (edge) or orange (face)\n"
              "  handles of the selected brushes; Ctrl+click adds handles; in 3D the\n"
              "  drag is horizontal, Alt drags vertically.  Moves that would make a\n"
              "  brush concave are refused.\n"
              "  Click a view's label to change its type; Shift+Space maximizes the\n"
              "  active view; drag the gap between views to resize them.\n\n"
              "KEYS (Edit > Preferences > Keyboard)\n");
    for (i = 0; i < num_actions; i++) {
        char keys[128];
        keybind_format(&actions[i], keys, sizeof(keys));
        sb_appendf(&sb, "  %-36s %s\n", actions[i].label, keys[0] ? keys : "-");
    }
    ui_text_set(keys_text, sb.data);
    sb_free(&sb);
}

void ui_open_keys_help(void)
{
    if (!keys_dlg) {
        Widget btns;
        keys_dlg = ui_form_dialog("keyshelp", "Keyboard and Mouse");
        btns = hrow(keys_dlg);
        XtVaSetValues(btns, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
        ui_button(btns, "Close", close_cb, keys_dlg);
        keys_text = scrolled_text(keys_dlg, "keys", 30, 84, 0);
        XtVaSetValues(XtParent(keys_text),
                      XmNtopAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, btns,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    }
    keys_fill();
    raise_dialog(keys_dlg);
}

/* ------------------------------------------------------------------ */
/* notifications from the main window                                  */

void dlg_selection_changed(void)
{
    ent_refresh(1);
    face_refresh();
}

void dlg_textures_changed(void)
{
    if (tbw.dlg && XtIsManaged(tbw.dlg)) {
        tb_update_info();
        ui_request_redraw_browser();
    }
}

void dlg_entities_changed(void)
{
    ent_refresh(1);
}

void dlg_refresh_accelerators(void)
{
    keys_fill();
}
