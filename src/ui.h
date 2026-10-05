/*
 * ui.h - services the toolkit front end provides to the editor core
 */
#ifndef QOTIF_UI_H
#define QOTIF_UI_H

#include <stddef.h>

struct view_s;

enum { CURSOR_NORMAL, CURSOR_HIDDEN, CURSOR_CROSS, CURSOR_MOVE };

int ui_main(int argc, char **argv);

void ui_redraw_all(void);
void ui_redraw_view(struct view_s *v);
void ui_status(const char *fmt, ...);
void ui_selection_changed(void);
void ui_map_changed(void);
void ui_tool_changed(void);
void ui_textures_changed(void);
void ui_entities_changed(void);
void ui_sync_toggles(void);
void ui_layout_changed(void);
void ui_maximize_view(struct view_s *v);

void ui_popup_view_menu(struct view_s *v, int x, int y);
void ui_popup_context(struct view_s *v, int x, int y);
void ui_set_cursor(struct view_s *v, int cursor);
void ui_warp_pointer(struct view_s *v, int x, int y);
void ui_fly_start(void);
int ui_grab_pointer(struct view_s *v, int grab);
void ui_console_changed(void);

void ui_message(const char *msg);
int ui_confirm(const char *msg);
int ui_ask_save(void);
int ui_prompt(const char *title, const char *label, char *buf, size_t size);
int ui_file_dialog(const char *title, const char *pattern, int save, char *out, size_t size);

void ui_open_entity_dialog(void);
void ui_open_face_dialog(void);
void ui_open_texture_browser(void);
void ui_open_prefs_dialog(void);
void ui_open_compile_dialog(void);
void ui_open_keys_help(void);
void ui_quit(void);

unsigned long ui_keysym(const char *name);

#endif
