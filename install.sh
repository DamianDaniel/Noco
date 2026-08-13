#!/bin/sh
set -e

PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"
XSESSIONS="/usr/share/xsessions"

echo "Building all components..."
for d in wm panel launcher notifyd settings; do
    make -C "$d" clean
    make -C "$d"
done

echo "Installing binaries to $BINDIR..."
install -Dm755 wm/noco-wm "$BINDIR/noco-wm"
install -Dm755 panel/noco-panel "$BINDIR/noco-panel"
install -Dm755 launcher/noco-launcher "$BINDIR/noco-launcher"
install -Dm755 notifyd/noco-notifyd "$BINDIR/noco-notifyd"
install -Dm755 settings/noco-settings "$BINDIR/noco-settings"
install -Dm755 session/noco-session "$BINDIR/noco-session"

if [ -d "$XSESSIONS" ] || [ -w "$(dirname $XSESSIONS)" ]; then
    echo "Installing X session entry..."
    install -Dm644 session/noco.desktop "$XSESSIONS/noco.desktop" 2>/dev/null || \
        echo "Skipped: could not write $XSESSIONS (needs root)"
fi

echo "Installing default config to ~/.config/noco/noco.conf (if absent)..."
mkdir -p "$HOME/.config/noco"
if [ ! -f "$HOME/.config/noco/noco.conf" ]; then
    cp config/noco.conf "$HOME/.config/noco/noco.conf"
fi

echo "Done. Log out and select 'Noco' at your display manager, or run 'startx' with:"
echo "  exec noco-session"
echo "in ~/.xinitrc"
