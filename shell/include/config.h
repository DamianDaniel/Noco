#ifndef NOCO_CONFIG_H
#define NOCO_CONFIG_H

typedef enum {
    NOCO_PANEL_TOP,
    NOCO_PANEL_BOTTOM
} PanelPosition;

typedef struct {
    unsigned int mod_mask;
    int gap;
    int border_width;
    unsigned long border_color;
    unsigned long focus_color;

    int panel_height;
    int corner_radius;
    double tint_r, tint_g, tint_b, tint_alpha;
    double border_alpha;
    double highlight_alpha;
    int blur_downscale;
    PanelPosition panel_position;

    int workspace_count;
    char launcher_cmd[256];
    char terminal_cmd[256];

    int notif_width;
    int notif_default_timeout_ms;
    int notif_margin;
    int notif_spacing;
    char notif_position[16];
} NocoConfig;

void noco_config_load(NocoConfig *cfg);

#endif
