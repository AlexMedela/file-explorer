// addons.c - hooks for features compiled in from another source folder (included from app.c, before units.c)
//
// A build with FE_ADDON_SRC=<dir> (build.sh) adds -DFE_ADDON -I<dir> and includes <dir>/addon.c at the end of app.c,
// after selftest.c, so add-on code can use anything in the app; it must define every hook declared here. A normal
// build gets the empty defaults below.
//
// What add-on code owns: sidebar rows PK_ADDONHDR / PK_ADDON / PK_ADDONEMPTY (Place.addon tells its sections apart),
// the actions ACT_ADDON_BASE .. ACT_ADDON_LAST, the sheet SHEET_ADDON and its own settings.conf keys (a build without
// it keeps keys it doesn't know, so they survive).

// startup and the main loop
static int addon_main(int argc, char **argv);          // a helper mode run instead of the app (-1: none)
static void addon_start(bool sync);                    // after the first init_places; sync: may change the system (not in tests)
static void addon_tick(void);                          // every frame
static int addon_wait_ms(void);                        // > 0: the idle loop wakes within this many ms
// settings.conf
static void addon_settings_defaults(void);
static bool addon_settings_line(const char *k, const char *v); // true: the key is the add-on's
static void addon_settings_save(FILE *f);
// sidebar
static void addon_places(void);                        // append sections (place_row) after the built-in ones
static bool addon_section_collapsed(const Place *pl);  // a PK_ADDONHDR's chevron
static void addon_header_click(Place *pl, bool plus);  // left click on a PK_ADDONHDR (plus: on its + button)
static void addon_place_draw(Place *pl, Rect r, bool hover);
static void addon_place_click(Place *pl, int button);  // a PK_ADDON row
static void addon_place_menu(Place *pl);               // right-click on a PK_ADDONHDR / PK_ADDON row: menu_add() its items
static void addon_action(int act);                     // ACT_ADDON_BASE .. ACT_ADDON_LAST; A.menu_place is the row
// tray menu (between "Open" and "Quit")
static void addon_tray_sig(char *sig, size_t cap);     // changes whenever the entries would
static void addon_tray_entries(SDL_TrayMenu *m, int *pos);
// the sheet SHEET_ADDON (ui_tools.c); the connect dialog's icon picker works for it too
static void addon_sheet_draw(void);
static bool addon_sheet_key(SDL_Keycode k, SDL_Keymod mod); // true: handled (else Escape closes the sheet)
static void addon_sheet_text(const char *text);
static void addon_sheet_click(f32 x, f32 y);           // left button, anywhere (outside the sheet too); picker clicks are handled
static const char *addon_sheet_icon(void);             // the icon picker's default icon key
// self-tests
static void addon_selftest_m10(void);                  // inside --selftest-m10, with its stand-ins (systemctl, journalctl, ssh)

#ifndef FE_ADDON
static int addon_main(int argc, char **argv) { return -1; }
static void addon_start(bool sync) {}
static void addon_tick(void) {}
static int addon_wait_ms(void) { return 0; }
static void addon_settings_defaults(void) {}
static bool addon_settings_line(const char *k, const char *v) { return false; }
static void addon_settings_save(FILE *f) {}
static void addon_places(void) {}
static bool addon_section_collapsed(const Place *pl) { return false; }
static void addon_header_click(Place *pl, bool plus) {}
static void addon_place_draw(Place *pl, Rect r, bool hover) {}
static void addon_place_click(Place *pl, int button) {}
static void addon_place_menu(Place *pl) {}
static void addon_action(int act) {}
static void addon_tray_sig(char *sig, size_t cap) { sig[0] = 0; }
static void addon_tray_entries(SDL_TrayMenu *m, int *pos) {}
static void addon_sheet_draw(void) {}
static bool addon_sheet_key(SDL_Keycode k, SDL_Keymod mod) { return false; }
static void addon_sheet_text(const char *text) {}
static void addon_sheet_click(f32 x, f32 y) {}
static const char *addon_sheet_icon(void) { return ""; }
static void addon_selftest_m10(void) {}
#endif
