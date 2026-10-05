/*
 * actions.h - named editor actions and their configurable key bindings
 *
 * Every menu item and shortcut goes through this table.  Bindings are
 * stored in the [keys] section of prefs.cfg as "action = Ctrl+Shift+S"
 * (two bindings may be given separated by a comma).
 */
#ifndef QOTIF_ACTIONS_H
#define QOTIF_ACTIONS_H

#define MAX_BINDINGS 2

typedef struct keybind_s {
    int mods;               /* MOD_SHIFT | MOD_CTRL | MOD_ALT */
    char key[32];           /* X keysym name or a single character */
    unsigned long sym;      /* resolved by the UI */
} keybind_t;

typedef struct action_s {
    const char *name;
    const char *label;
    const char *defkeys;
    void (*fn)(void);
    int *toggle;            /* non-NULL for check items */
    int fly;                /* camera fly bit for movement keys */
    keybind_t keys[MAX_BINDINGS];
    int nkeys;
    void *widget;           /* menu item, set by the UI */
} action_t;

extern action_t actions[];
extern int num_actions;

action_t *action_find(const char *name);
void action_run(action_t *a);
int action_run_name(const char *name);
action_t *action_for_key(int mods, unsigned long sym);
action_t *fly_action_for_key(unsigned long sym);

void actions_load_bindings(void);
void actions_save_bindings(void);
void actions_reset_binding(action_t *a);
int keybind_parse(const char *s, keybind_t *out, int max);
void keybind_format(const action_t *a, char *buf, int size);

#endif
