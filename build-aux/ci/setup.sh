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

    mv ${FLATPAK_MANIFEST} ${FLATPAK_DEVEL_MANIFEST} || true


    echo "  - Changing app ID to org.gnome.TodoDevel"
    sed -i "s,\"org\.gnome\.$PROJECT_ID\",\"org\.gnome\.$PROJECT_DEVEL_ID\",g" ${FLATPAK_DEVEL_MANIFEST}

    for file in $(grep -rl "org\.gnome\.$PROJECT_ID" data po plugins src)
    do
        echo "        Modifying $file"
        sed -i "s,org\.gnome\.$PROJECT_ID,org\.gnome\.$PROJECT_DEVEL_ID,g" $file
    done


    echo "  - Renaming files"
    for file in `find . -type f -name "*org.gnome.$PROJECT_ID.*"`
    do
        new_filename=$(sed "s,org\.gnome\.$PROJECT_ID,org\.gnome\.$PROJECT_DEVEL_ID,g" <<< $file)

        echo "        Moving $file to $new_filename"
        mv $file $new_filename || true
    done


    echo "  - Pointing Flatpak Manifest to the current branch"
    sed -i $"145i\
    ,\n\
    \"branch\" : \"$BRANCH\"\,\n\
    \"path\" : \".\"\n\
    " $FLATPAK_DEVEL_MANIFEST

#    echo "  - Adding desktop file rename to Flatpak manifest"
#    sed -i $"11i\
#    \"rename-desktop-file\" \: \"org\.gnome\.$PROJECT_ID\.desktop\",\n\
#    \"rename-icon\" \: \"org\.gnome\.$PROJECT_ID\"," ${FLATPAK_DEVEL_MANIFEST}
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
