# Noco

Noco is a lightweight Linux desktop environment built for X11, with no compositor and minimal dependencies. It focuses on a clean, coherent desktop experience using window decorations, wallpaper sampling, cached snapshots, and theme rules instead of live compositing.

![Hero screenshot placeholder](docs/images/hero.png)

## What Noco is

Noco is a full desktop environment, not just a window manager. It includes a panel, launcher, notification daemon, settings app, session integration, and shared shell components.

![Desktop overview placeholder](docs/images/desktop-overview.png)

## Highlights

- No compositor
- Minimal dependencies: X11 + GTK3
- Pseudo-transparency from wallpaper sampling
- Cached window previews for lightweight task switching
- Simple configuration and predictable behavior
- Designed to feel complete, polished, and fast

![Launcher placeholder](docs/images/launcher.png)
![Panel placeholder](docs/images/panel.png)
![Notifications placeholder](docs/images/notifications.png)

## Components

- `wm/` — `noco-wm`: reparenting X11 window manager
- `panel/` — `noco-panel`: top/bottom dock, workspace switcher, clock
- `launcher/` — `noco-launcher`: rounded app search popup
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

Noco is for people who want a desktop that feels modern and polished without the cost of a compositor. It aims for the kind of integrated, approachable experience people expect from a full desktop environment, while staying small and hackable.

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

Or select **Noco** from your display manager’s session list.

![Login/session placeholder](docs/images/session.png)

## Keybindings

Default mod key: **Super**

- `Mod+Drag` — move window
- `Mod+Right Drag` — resize window
- `Mod+F` — toggle maximize
- `Mod+M` — minimize
- `Mod+Q` — close window
- `Mod+Arrow Keys` — snap window to half/full screen
- `Mod+1..9` — switch workspace
- `Mod+Shift+1..9` — move focused window to workspace
- `Mod+Tab` — task switcher with previews
- `Mod+Shift+Tab` — reverse task switcher
- `Mod+D` — app launcher
- `Mod+Return` — terminal

## Configuration

Edit `~/.config/noco/noco.conf` directly, or run `noco-settings`.

If no user config exists, Noco falls back to:

1. `/etc/noco/noco.conf`
2. built-in defaults

See `config/noco.conf` for all keys.

Changes require restarting the affected component (`noco-wm`, `noco-panel`, `noco-notifyd`) to take effect.

![Config editor placeholder](docs/images/config-editor.png)

## How pseudo-transparency works

Each shell surface grabs the desktop’s root pixmap (`_XROOTPMAP_ID`) once and caches it as a full-screen cairo surface. On draw, it samples the relevant screen region from that cache, box-downscales it for a cheap blur-like effect, tints it, and clips to a rounded rectangle.

The cache is only re-captured when the wallpaper changes, never per-frame and never through live compositing.

![Wallpaper sampling diagram placeholder](docs/images/wallpaper-sampling-diagram.png)

## Known limitations

- Window previews can go stale when windows are overlapped on the same workspace.
- No multi-monitor-aware panel/launcher placement yet.
- No lock screen yet.
- Notification daemon currently has no persistent history and no action buttons.

## Roadmap

- Better multi-monitor support
- Lock screen
- Window preview refresh improvements
- More theme options
- Notification actions and history
- Polished onboarding experience

## Contributing

Bug reports, patches, themes, packaging help, and design feedback are welcome.

## License



## Gallery

![Gallery placeholder 1](docs/images/gallery-1.png)
![Gallery placeholder 2](docs/images/gallery-2.png)
![Gallery placeholder 3](docs/images/gallery-3.png)
```

If you want, I can also turn this into a more “big desktop project” style README with a stronger front page and better marketing copy.