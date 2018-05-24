#!/bin/bash -e

BRANCH=$2
PROJECT_ID=Todo
PROJECT_DEVEL_ID=$1
PROJECT_NAME=gnome-todo
FLATPAK_MANIFEST=org.gnome.$PROJECT_ID.json
FLATPAK_DEVEL_MANIFEST=org.gnome.$PROJECT_DEVEL_ID.json
FLATPAK_DIR=build-aux/flatpak

function copy_flatpak_files() {

    echo "Copying Flatpak manifest to root folder"

    cp -R ${FLATPAK_DIR}/* . || true
}

function modify_manifest() {

    echo "Modifying Flatpak manifest file…"

    # Replace all instances of "org.gnome.Todo" from every file
    echo "  - Changing app ID to org.gnome.TodoDevel"
    sed -i "s,\"org\.gnome\.$PROJECT_ID\",\"org\.gnome\.$PROJECT_DEVEL_ID\",g" ${FLATPAK_DEVEL_MANIFEST}

    for file in $(grep -rl "org\.gnome\.$PROJECT_ID" data po plugins src)
    do
        echo "        Modifying $file"
        sed -i "s,org\.gnome\.$PROJECT_ID,org\.gnome\.$PROJECT_DEVEL_ID,g" $file
    done


    # org.gnome.Todo.* → org.gnome.<New Suffix>.*
    echo "  - Renaming files"
    for file in `find . -type f -name "*org.gnome.$PROJECT_ID.*"`
    do
        new_filename=$(sed "s,org\.gnome\.$PROJECT_ID,org\.gnome\.$PROJECT_DEVEL_ID,g" <<< $file)

        echo "        Moving $file to $new_filename"
        mv $file $new_filename || true
    done

    # Makes org.gnome.<NewSuffix>.json point to .
    echo "  - Pointing Flatpak Manifest to the current branch"
    sed -i $"145i\
    ,\n\
    \"branch\" : \"$BRANCH\"\,\n\
    \"path\" : \".\"\n\
    " $FLATPAK_DEVEL_MANIFEST

    ls -la .
}

# ----------------------

echo ""
echo "Building Flatpak"
echo ""

# Copy all the files to the root folder
copy_flatpak_files

# Change the app id to make it parallel installable with stable To Do.
modify_manifest

echo ""
echo "Done"
echo ""
