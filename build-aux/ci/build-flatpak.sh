#!/bin/bash -e

PROJECT_ID=org.gnome.$1
BUNDLE=$PROJECT_ID.flatpak
MANIFEST=$PROJECT_ID.json
RUNTIME_REPO="https://sdk.gnome.org/gnome-nightly.flatpakrepo"

#flatpak-builder --stop-at=gnome-todo --bundle-sources --repo=repo app $MANIFEST

#flatpak build app meson . _build --prefix=/app -Dtracing=true
#flatpak build app ninja -C _build install
#flatpak-builder --finish-only --repo=repo app $MANIFEST

flatpak-builder --bundle-sources --repo=repo app $MANIFEST
flatpak build-bundle repo $BUNDLE --runtime-repo=$RUNTIME_REPO $PROJECT_ID 
