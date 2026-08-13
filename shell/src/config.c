#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <X11/X.h>
#include "config.h"

static void set_defaults(NocoConfig *cfg) {
    cfg->mod_mask = Mod4Mask;
    cfg->gap = 4;
    cfg->border_width = 2;
    cfg->border_color = 0x3a3a3a;
    cfg->focus_color = 0x5e9eff;

    cfg->panel_height = 34;
    cfg->corner_radius = 10;
    cfg->tint_r = 0.10;
    cfg->tint_g = 0.10;
    cfg->tint_b = 0.13;
    cfg->tint_alpha = 0.55;
    cfg->border_alpha = 0.12;
    cfg->highlight_alpha = 0.06;
    cfg->blur_downscale = 6;
    cfg->panel_position = NOCO_PANEL_TOP;

    cfg->workspace_count = 9;

    g_strlcpy(cfg->launcher_cmd, "noco-launcher", sizeof(cfg->launcher_cmd));
    g_strlcpy(cfg->terminal_cmd, "x-terminal-emulator", sizeof(cfg->terminal_cmd));

    cfg->notif_width = 320;
    cfg->notif_default_timeout_ms = 5000;
    cfg->notif_margin = 12;
    cfg->notif_spacing = 8;
    g_strlcpy(cfg->notif_position, "top-right", sizeof(cfg->notif_position));
}

static unsigned int parse_mod_key(const char *s) {
    if (!s) return Mod4Mask;
    if (g_ascii_strcasecmp(s, "alt") == 0 || g_ascii_strcasecmp(s, "mod1") == 0)
        return Mod1Mask;
    if (g_ascii_strcasecmp(s, "super") == 0 || g_ascii_strcasecmp(s, "mod4") == 0)
        return Mod4Mask;
    return Mod4Mask;
}

static unsigned long parse_color(const char *s, unsigned long fallback) {
    if (!s || !*s) return fallback;
    if (s[0] == '#') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    char *end;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s) return fallback;
    return v;
}

static char *find_config_path(void) {
    const char *xdg = g_get_user_config_dir();
    char *path = g_build_filename(xdg, "noco", "noco.conf", NULL);
    if (g_file_test(path, G_FILE_TEST_EXISTS)) return path;
    g_free(path);

    path = g_strdup("/etc/noco/noco.conf");
    if (g_file_test(path, G_FILE_TEST_EXISTS)) return path;
    g_free(path);

    return NULL;
}

void noco_config_load(NocoConfig *cfg) {
    set_defaults(cfg);

    char *path = find_config_path();
    if (!path) return;

    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
        g_error_free(err);
        g_key_file_free(kf);
        g_free(path);
        return;
    }

    GError *e = NULL;

    char *modkey = g_key_file_get_string(kf, "general", "mod_key", &e);
    if (modkey) { cfg->mod_mask = parse_mod_key(modkey); g_free(modkey); }
    g_clear_error(&e);

    int gap = g_key_file_get_integer(kf, "general", "gap", &e);
    if (!e) cfg->gap = gap;
    g_clear_error(&e);

    int bw = g_key_file_get_integer(kf, "general", "border_width", &e);
    if (!e) cfg->border_width = bw;
    g_clear_error(&e);

    int wc = g_key_file_get_integer(kf, "general", "workspace_count", &e);
    if (!e && wc > 0 && wc <= 9) cfg->workspace_count = wc;
    g_clear_error(&e);

    char *bc = g_key_file_get_string(kf, "theme", "border_color", &e);
    if (bc) { cfg->border_color = parse_color(bc, cfg->border_color); g_free(bc); }
    g_clear_error(&e);

    char *fc = g_key_file_get_string(kf, "theme", "focus_color", &e);
    if (fc) { cfg->focus_color = parse_color(fc, cfg->focus_color); g_free(fc); }
    g_clear_error(&e);

    int ph = g_key_file_get_integer(kf, "theme", "panel_height", &e);
    if (!e) cfg->panel_height = ph;
    g_clear_error(&e);

    int cr = g_key_file_get_integer(kf, "theme", "corner_radius", &e);
    if (!e) cfg->corner_radius = cr;
    g_clear_error(&e);

    double tr = g_key_file_get_double(kf, "theme", "tint_r", &e);
    if (!e) cfg->tint_r = tr;
    g_clear_error(&e);

    double tg = g_key_file_get_double(kf, "theme", "tint_g", &e);
    if (!e) cfg->tint_g = tg;
    g_clear_error(&e);

    double tb = g_key_file_get_double(kf, "theme", "tint_b", &e);
    if (!e) cfg->tint_b = tb;
    g_clear_error(&e);

    double ta = g_key_file_get_double(kf, "theme", "tint_alpha", &e);
    if (!e) cfg->tint_alpha = ta;
    g_clear_error(&e);

    double ba = g_key_file_get_double(kf, "theme", "border_alpha", &e);
    if (!e) cfg->border_alpha = ba;
    g_clear_error(&e);

    double ha = g_key_file_get_double(kf, "theme", "highlight_alpha", &e);
    if (!e) cfg->highlight_alpha = ha;
    g_clear_error(&e);

    int bd = g_key_file_get_integer(kf, "theme", "blur_downscale", &e);
    if (!e && bd >= 1) cfg->blur_downscale = bd;
    g_clear_error(&e);

    char *pos = g_key_file_get_string(kf, "panel", "position", &e);
    if (pos) {
        cfg->panel_position = (g_ascii_strcasecmp(pos, "bottom") == 0)
            ? NOCO_PANEL_BOTTOM : NOCO_PANEL_TOP;
        g_free(pos);
    }
    g_clear_error(&e);

    char *launcher_cmd = g_key_file_get_string(kf, "bindings", "launcher_cmd", &e);
    if (launcher_cmd) {
        g_strlcpy(cfg->launcher_cmd, launcher_cmd, sizeof(cfg->launcher_cmd));
        g_free(launcher_cmd);
    }
    g_clear_error(&e);

    char *terminal_cmd = g_key_file_get_string(kf, "bindings", "terminal_cmd", &e);
    if (terminal_cmd) {
        g_strlcpy(cfg->terminal_cmd, terminal_cmd, sizeof(cfg->terminal_cmd));
        g_free(terminal_cmd);
    }
    g_clear_error(&e);

    int nw = g_key_file_get_integer(kf, "notifications", "width", &e);
    if (!e) cfg->notif_width = nw;
    g_clear_error(&e);

    int nt = g_key_file_get_integer(kf, "notifications", "default_timeout_ms", &e);
    if (!e) cfg->notif_default_timeout_ms = nt;
    g_clear_error(&e);

    int nm = g_key_file_get_integer(kf, "notifications", "margin", &e);
    if (!e) cfg->notif_margin = nm;
    g_clear_error(&e);

    int ns = g_key_file_get_integer(kf, "notifications", "spacing", &e);
    if (!e) cfg->notif_spacing = ns;
    g_clear_error(&e);

    char *npos = g_key_file_get_string(kf, "notifications", "position", &e);
    if (npos) {
        g_strlcpy(cfg->notif_position, npos, sizeof(cfg->notif_position));
        g_free(npos);
    }
    g_clear_error(&e);

    g_key_file_free(kf);
    g_free(path);
}
