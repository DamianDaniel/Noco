# Noco

A Linux desktop environment with no compositor. All visual depth and
transparency come from window decorations, wallpaper sampling, cached
snapshots, and theme rules — not from a compositing manager.

## Components

- `wm/` — noco-wm: reparenting X11 window manager (Xlib, no compositing)
- `panel/` — noco-panel: top/bottom dock, workspace switcher, clock
- `launcher/` — noco-launcher: rounded app search popup
- `notifyd/` — noco-notifyd: org.freedesktop.Notifications daemon, stacked popups
- `settings/` — noco-settings: GUI editor for noco.conf
- `shell/` — shared library: config loader, wallpaper sampling cache, renderer
- `session/` — session script and .desktop entry
- `config/` — default noco.conf

## Build

Requires: gcc, make, libx11-dev, libxrandr-dev, libxinerama-dev,
libgtk-3-dev, glib2.0-dev.

```
sudo ./install.sh
```

Builds and installs all five binaries to `/usr/local/bin`, installs an
Xsession entry if `/usr/share/xsessions` is writable, and copies a
default config to `~/.config/noco/noco.conf` if none exists.

To build a single component: `make -C wm`, `make -C panel`, etc.

## Running

Add to `~/.xinitrc`:

```
exec noco-session
```

or select "Noco" from your display manager's session list.

## Keybindings (default, Mod = Super)

- `Mod+drag` — move window
- `Mod+right-drag` — resize window
- `Mod+F` — toggle maximize
- `Mod+M` — minimize
- `Mod+Q` — close window
- `Mod+Left/Right/Up/Down` — snap window to half/full screen
- `Mod+1..9` — switch workspace
- `Mod+Shift+1..9` — move focused window to workspace
- `Mod+Tab` (hold Mod, tap Tab, release Mod) — task switcher with previews
- `Mod+Shift+Tab` — task switcher, reverse direction
- `Mod+D` — app launcher
- `Mod+Return` — terminal

## Configuration

Edit `~/.config/noco/noco.conf` directly, or run `noco-settings`.
Falls back to `/etc/noco/noco.conf`, then built-in defaults if neither
exists. See `config/noco.conf` for all keys. Changes require restarting
the affected component (`noco-wm`, `noco-panel`, `noco-notifyd`) to
take effect — nothing hot-reloads.

## How pseudo-transparency works

Each shell surface (panel, launcher, notification popup, settings
window) grabs the desktop's root pixmap (`_XROOTPMAP_ID`) once and
caches it as a full-screen cairo surface. On draw, it samples just its
own screen region from that cache, box-downscales it for a cheap
blur-like effect, tints it, and clips to a rounded rect. The cache is
only re-captured when the wallpaper itself changes (`PropertyNotify`
on the root pixmap atom) — never per-frame, never via live compositing
of overlapping windows.

## Known limitations

- **Window previews can go stale.** Thumbnails for the task switcher
  are captured at the moment a window is minimized or its workspace is
  switched away from (the one point its pixels are guaranteed valid
  without a compositor). Windows that stay visible but get overlapped
  by a sibling on the *same* workspace keep their last-captured
  thumbnail until the next minimize/switch event. This is a direct
  consequence of not compositing — X11 has no reliable way to read
  pixels hidden behind other windows without one.
- No multi-monitor-aware panel/launcher placement yet — everything
  targets the primary monitor via `gdk_display_get_primary_monitor`.
- No lock screen yet (listed in the original MVP, not yet built).
- Notification daemon has no persistent history and no action buttons,
  only body text and click-to-dismiss.
