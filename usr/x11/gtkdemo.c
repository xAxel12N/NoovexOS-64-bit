#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

typedef void* P;
void gtk_init(int* argc, char*** argv);
P gtk_window_new(int type);
void gtk_window_set_title(P w, const char* t);
void gtk_window_set_default_size(P w, int a, int b);
P gtk_box_new(int orient, int spacing);
void gtk_box_pack_start(P box, P child, int expand, int fill, unsigned pad);
void gtk_container_add(P c, P w);
void gtk_container_set_border_width(P c, unsigned w);
P gtk_label_new(const char* s);
void gtk_label_set_markup(P l, const char* s);
void gtk_label_set_xalign(P l, float x);
P gtk_entry_new(void);
const char* gtk_entry_get_text(P e);
void gtk_entry_set_placeholder_text(P e, const char* s);
P gtk_button_new_with_label(const char* s);
P gtk_switch_new(void);
int gtk_switch_get_active(P s);
void gtk_switch_set_active(P s, int on);
P gtk_scale_new_with_range(int orient, double lo, double hi, double step);
double gtk_range_get_value(P r);
void gtk_range_set_value(P r, double v);
P gtk_progress_bar_new(void);
void gtk_progress_bar_set_fraction(P p, double f);
void gtk_progress_bar_set_show_text(P p, int on);
P gtk_combo_box_text_new(void);
void gtk_combo_box_text_append_text(P c, const char* s);
void gtk_combo_box_set_active(P c, int i);
char* gtk_combo_box_text_get_active_text(P c);
P gtk_text_view_new(void);
P gtk_text_view_get_buffer(P v);
void gtk_text_buffer_set_text(P b, const char* s, int n);
P gtk_scrolled_window_new(P h, P v);
void gtk_widget_set_size_request(P w, int a, int b);
void gtk_widget_show_all(P w);
void gtk_main(void);
void gtk_main_quit(void);
unsigned gtk_get_major_version(void);
unsigned gtk_get_minor_version(void);
unsigned gtk_get_micro_version(void);
unsigned long g_signal_connect_data(P inst, const char* sig, void* cb, P data, void* destroy, int flags);
unsigned g_timeout_add(unsigned ms, int (*fn)(P), P data);
void g_free(P p);

static P lbl, ent, chk, scl, bar, cmb, buf;
static int selftest;

static void on_hello(P b, P d) {
    (void)b; (void)d;
    char t[512];
    const char* s = gtk_entry_get_text(ent);
    char* c = gtk_combo_box_text_get_active_text(cmb);
    snprintf(t, sizeof t, "<b>Hello, %s!</b>  Colour: %s  Bold: %s  Slider: %.0f", s && *s ? s : "world", c ? c : "none", gtk_switch_get_active(chk) ? "yes" : "no", gtk_range_get_value(scl));
    if (c) g_free(c);
    gtk_label_set_markup(lbl, t);
}
static void on_scale(P r, P d) { (void)d; gtk_progress_bar_set_fraction(bar, gtk_range_get_value(r) / 100.0); }
static void on_quit(P b, P d) { (void)b; (void)d; gtk_main_quit(); }
static int on_selftest(P d) {
    (void)d;
    on_hello(0, 0);
    FILE* f = fopen("/tmp/gtkdemo.ok", "w");
    if (f) { fprintf(f, "GTK %u.%u.%u window shown\n", gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version()); fclose(f); }
    printf("gtkdemo: GTK %u.%u.%u window shown\n", gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version());
    fflush(stdout);
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--selftest")) selftest = 1;
    if (!getenv("DISPLAY")) setenv("DISPLAY", "127.0.0.1:0", 1);
    setenv("GDK_BACKEND", "x11", 0);
    setenv("NO_AT_BRIDGE", "1", 0);
    setenv("GTK_A11Y", "none", 0);
    setenv("GSETTINGS_BACKEND", "memory", 0);
    if (!getenv("HOME")) setenv("HOME", "/root", 1);
    if (!getenv("XDG_CACHE_HOME")) setenv("XDG_CACHE_HOME", "/root/.cache", 1);
    if (!getenv("XDG_RUNTIME_DIR")) { mkdir("/tmp/xdg-runtime", 0700); setenv("XDG_RUNTIME_DIR", "/tmp/xdg-runtime", 1); }
    gtk_init(&argc, &argv);
    P win = gtk_window_new(0);
    char title[96];
    snprintf(title, sizeof title, "GTK %u.%u on NoovexOS", gtk_get_major_version(), gtk_get_minor_version());
    gtk_window_set_title(win, title);
    gtk_window_set_default_size(win, 520, 420);
    gtk_container_set_border_width(win, 12);
    g_signal_connect_data(win, "destroy", (void*)on_quit, 0, 0, 0);
    P box = gtk_box_new(1, 8);
    gtk_container_add(win, box);
    lbl = gtk_label_new(0);
    gtk_label_set_markup(lbl, "<big><b>GTK 3 is running on NoovexOS</b></big>\nType a name and press Say hello.");
    gtk_label_set_xalign(lbl, 0);
    gtk_box_pack_start(box, lbl, 0, 0, 0);
    ent = gtk_entry_new();
    gtk_entry_set_placeholder_text(ent, "Your name");
    gtk_box_pack_start(box, ent, 0, 0, 0);
    P row = gtk_box_new(0, 8);
    gtk_box_pack_start(row, gtk_label_new("Bold"), 0, 0, 0);
    chk = gtk_switch_new();
    gtk_switch_set_active(chk, 1);
    gtk_box_pack_start(row, chk, 0, 0, 0);
    cmb = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(cmb, "Blue");
    gtk_combo_box_text_append_text(cmb, "Green");
    gtk_combo_box_text_append_text(cmb, "Orange");
    gtk_combo_box_set_active(cmb, 0);
    gtk_box_pack_start(row, cmb, 0, 0, 0);
    gtk_box_pack_start(box, row, 0, 0, 0);
    scl = gtk_scale_new_with_range(0, 0, 100, 1);
    gtk_range_set_value(scl, 40);
    g_signal_connect_data(scl, "value-changed", (void*)on_scale, 0, 0, 0);
    gtk_box_pack_start(box, scl, 0, 0, 0);
    bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(bar, 1);
    gtk_progress_bar_set_fraction(bar, 0.4);
    gtk_box_pack_start(box, bar, 0, 0, 0);
    P sw = gtk_scrolled_window_new(0, 0);
    gtk_widget_set_size_request(sw, -1, 120);
    P tv = gtk_text_view_new();
    buf = gtk_text_view_get_buffer(tv);
    gtk_text_buffer_set_text(buf, "This is a GtkTextView.\nThe window is drawn by GTK, Cairo and Pango\nthrough the NoovexOS X11 server.\n", -1);
    gtk_container_add(sw, tv);
    gtk_box_pack_start(box, sw, 1, 1, 0);
    P brow = gtk_box_new(0, 8);
    P hello = gtk_button_new_with_label("Say hello");
    g_signal_connect_data(hello, "clicked", (void*)on_hello, 0, 0, 0);
    P quit = gtk_button_new_with_label("Quit");
    g_signal_connect_data(quit, "clicked", (void*)on_quit, 0, 0, 0);
    gtk_box_pack_start(brow, hello, 1, 1, 0);
    gtk_box_pack_start(brow, quit, 1, 1, 0);
    gtk_box_pack_start(box, brow, 0, 0, 0);
    gtk_widget_show_all(win);
    if (selftest) g_timeout_add(1500, on_selftest, 0);
    gtk_main();
    return 0;
}
