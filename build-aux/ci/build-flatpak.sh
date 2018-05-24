#!/bin/bash -e

BUNDLE=org.gnome.$1.flatpak
MANIFEST=org.gnome.$1.json
RUNTIME_REPO="https://sdk.gnome.org/gnome-nightly.flatpakrepo"

flatpak-builder --stop-at=gnome-todo --bundle-sources --repo=repo app $MANIFEST

flatpak build app meson . _build --prefix=/app -Dtracing=true
flatpak build app ninja -C _build install
flatpak-builder --finish-only --repo=repo app $MANIFEST

flatpak build-bundle repo $BUNDLE --runtime-repo=$RUNTIME_REPO $MANIFEST 
