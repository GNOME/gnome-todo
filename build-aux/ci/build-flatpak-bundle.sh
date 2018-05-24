#!/bin/bash -e

PROJECT_ID=org.gnome.$1
BUNDLE=$PROJECT_ID.flatpak
MANIFEST=$PROJECT_ID.json
RUNTIME_REPO="https://sdk.gnome.org/gnome-nightly.flatpakrepo"

flatpak-builder --bundle-sources --repo=repo app $MANIFEST
flatpak build-bundle repo $BUNDLE --runtime-repo=$RUNTIME_REPO $PROJECT_ID 
