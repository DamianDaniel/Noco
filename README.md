# Noco

Noco is a lightweight Linux desktop environment built for X11, with no compositor and minimal dependencies. It looks and behaves like a classic desktop — title bars, a taskbar, a Start menu, mouse-driven window management — using window decorations, wallpaper sampling, cached snapshots, and theme rules instead of live compositing.

![Hero screenshot placeholder](docs/images/hero.png)

## What Noco is

Noco is a full desktop environment, not just a window manager. It includes decorated windows you move and resize with the mouse, a taskbar with a Start menu and a running-window list, a launcher, a notification daemon, a settings app, session integration, and shared shell components.

![Desktop overview placeholder](docs/images/desktop-overview.png)

## Highlights

- Classic, mouse-first desktop experience — click title bars to move, drag edges to resize, click buttons to close/minimize/maximize
- Taskbar with Start menu, running-window list, and workspace switcher
- No compositor, minimal dependencies: X11 + GTK3
- Pseudo-transparency from wallpaper sampling
- Cached window previews for lightweight task switching
- Simple configuration and predictable behavior
- Keyboard shortcuts available throughout, but nothing requires them

![Launcher placeholder](docs/images/launcher.png)
![Panel placeholder](docs/images/panel.png)
![Notifications placeholder](docs/images/notifications.png)

## Components

- `wm/` — `noco-wm`: reparenting X11 window manager with title bars, mouse move/resize, and window buttons
- `panel/` — `noco-panel`: taskbar with Start menu, running-window list, workspace switcher, and clock
- `launcher/` — `noco-launcher`: rounded app search popup, opened from the Start button
- `notifyd/` — `noco-notifyd`: notification daemon with stacked popups
- `settings/` — `noco-settings`: GUI editor for `noco.conf`
- `shell/` — shared library for config loading, wallpaper sampling cache, and rendering
- `session/` — session script and `.desktop` entry
- `config/` — default `noco.conf`

## Screenshots

![Workspace switching placeholder](docs/images/workspaces.png)
![Task switcher placeholder](docs/images/task-switcher.png)
![Settings placeholder](docs/images/settings.png)

## Why Noco

Noco is for people who want a desktop that feels modern, familiar, and polished without the cost of a compositor. It's built around the interactions people already know from Cinnamon, KDE, or Windows — title bars, a taskbar, a Start menu, drag-to-move, drag-to-resize — while staying small, hackable, and light on resources.

## Build

Requires: `gcc`, `make`, `libx11-dev`, `libxrandr-dev`, `libxinerama-dev`, `libgtk-3-dev`, `glib2.0-dev`.

```bash
sudo ./install.sh
```

This builds and installs all binaries to `/usr/local/bin`, installs an Xsession entry if `/usr/share/xsessions` is writable, and copies a default config to `~/.config/noco/noco.conf` if none exists.

To build a single component:

```bash
make -C wm
make -C panel
make -C launcher
make -C notifyd
make -C settings
```

## Running

Add this to `~/.xinitrc`:

```.xinitrc
exec noco-session
```

Or select **Noco** from your display manager's session list.

![Login/session placeholder](docs/images/session.png)

## Using Noco

Everything below works with the mouse alone. Keyboard shortcuts exist as shortcuts, not requirements.

**Windows** — each window has a title bar with the title on the left and three buttons on the right (minimize, maximize, close):

- Drag the title bar to move a window
- Double-click the title bar to maximize/restore
- Drag any window edge or corner to resize
- Click the square button to maximize/restore, the dash button to minimize, the cross button to close
- Click anywhere on a window to focus it (focus does not follow the mouse — only clicks change focus, like Windows/Cinnamon/KDE)

**Taskbar** — a bar docked to the bottom of the screen by default (configurable to the top):

- **Menu** button on the left opens the app launcher
- The middle section lists open windows on the current workspace; click a button to focus that window, click it again to minimize it
- The workspace switcher shows numbered buttons for each virtual desktop; click one to switch
- The clock sits on the right

**Launcher** — opened from the Menu button (or `Mod+D`): type to search installed applications, click or press Enter to launch.

**Notifications** — appear as stacked popups in a screen corner (configurable); click one to dismiss it early, or wait for it to time out.

## Keybindings (optional shortcuts)

Default mod key: **Super**

- `Mod+Drag` — move window from anywhere (not just the title bar)
- `Mod+Right Drag` — resize window from anywhere
- `Mod+F` — toggle maximize
- `Mod+M` — minimize
- `Mod+Q` — close window
- `Mod+Arrow Keys` — snap window to half/full screen
- `Mod+1..9` — switch workspace
- `Mod+Shift+1..9` — move focused window to workspace
- `Mod+Tab` (hold Mod, tap Tab, release Mod) — task switcher with previews
- `Mod+Shift+Tab` — reverse task switcher
- `Mod+D` — app launcher
- `Mod+Return` — terminal

## Configuration

Edit `~/.config/noco/noco.conf` directly, or run `noco-settings`.

If no user config exists, Noco falls back to:

1. `/etc/noco/noco.conf`
2. built-in defaults

Key sections include window decoration sizing (`titlebar_height`, `frame_margin`), taskbar position and colors, and notification placement. See `config/noco.conf` for all keys.

Changes require restarting the affected component (`noco-wm`, `noco-panel`, `noco-notifyd`) to take effect.

![Config editor placeholder](docs/images/config-editor.png)

## How pseudo-transparency works

Each shell surface — taskbar, launcher, notification popups, window title bars — grabs the desktop's root pixmap (`_XROOTPMAP_ID`) once and caches it as a full-screen cairo surface. On draw, it samples the relevant screen region from that cache, box-downscales it for a cheap blur-like effect, tints it, and clips to a rounded rectangle.

The cache is only re-captured when the wallpaper changes, never per-frame and never through live compositing.

![Wallpaper sampling diagram placeholder](docs/images/wallpaper-sampling-diagram.png)

## Known limitations

- Window previews can go stale when windows are overlapped on the same workspace, since X11 without a compositor can't reliably read pixels hidden behind other windows.
- No multi-monitor-aware panel/launcher placement yet — everything targets the primary monitor.
- No lock screen yet.
- Notification daemon currently has no persistent history and no action buttons.
- No hover-cursor change over resize edges yet (the resize zone works, it just doesn't preview with a cursor icon).

## Roadmap

- Better multi-monitor support
- Lock screen
- Resize-edge cursor hints
- Window preview refresh improvements
- More theme options
- Notification actions and history
- Polished onboarding experience

## Contributing

Bug reports, patches, themes, packaging help, and design feedback are welcome.

## License

Add your license here.

## Gallery

![Gallery placeholder 1](docs/images/gallery-1.png)
![Gallery placeholder 2](docs/images/gallery-2.png)
![Gallery placeholder 3](docs/images/gallery-3.png)
