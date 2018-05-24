#!/bin/bash -e

COMMIT=$2
PROJECT_ID=Todo
PROJECT_DEVEL_ID=$1
PROJECT_NAME=gnome-todo
FLATPAK_MANIFEST=org.gnome.$PROJECT_ID.json
FLATPAK_DEVEL_MANIFEST=org.gnome.$PROJECT_DEVEL_ID.json
FLATPAK_DIR=build-aux/flatpak

function copy_flatpak_files() {

    echo "Copying Flatpak manifest to root folder"

    cp -R ${FLATPAK_DIR}/* . || true

    # org.gnome.Todo.json → org.gnome.$PROJECT_DEVEL_ID.json
    mv ${FLATPAK_MANIFEST} ${FLATPAK_DEVEL_MANIFEST} || true
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
}

function rename_files() {

    # org.gnome.Todo.* → org.gnome.<New Suffix>.*
    echo "  - Renaming files"
    for file in `find . -type f -name "*org.gnome.$PROJECT_ID[.-]*"`
    do
        new_filename=$(sed "s,org\.gnome\.$PROJECT_ID,org\.gnome\.$PROJECT_DEVEL_ID,g" <<< $file)

        echo "        Moving $file to $new_filename"
        mv $file $new_filename || true
    done
}

function change_module_type() {

    # This function replaces the "type" : "git" and "url" : "..." by
    # "type" : "dir" and "path" : ".". This allows us to just run
    # `flatpak-builder --repo=repo <JSON Manifest>

    echo "  - Pointing Flatpak Manifest to the current directory"

    # Find the last "{" and use that to calculate the header and tail
    # of the new file
    n_lines=$(wc -l < $FLATPAK_DEVEL_MANIFEST)
    head_lines=$(grep -n "{" org.gnome.TodoDevel.json | cut -f1 -d: | tail -n 1)

    header=$(head -n $head_lines $FLATPAK_DEVEL_MANIFEST)
    tail=$(tail -n 5 $FLATPAK_DEVEL_MANIFEST)

    echo "$header" > $FLATPAK_DEVEL_MANIFEST
    echo '                    "type" : "dir",' >> $FLATPAK_DEVEL_MANIFEST
    echo '                    "path" : "."' >> $FLATPAK_DEVEL_MANIFEST
    echo "$tail" >> $FLATPAK_DEVEL_MANIFEST
}

function print_new_files() {

    cat $FLATPAK_DEVEL_MANIFEST

    ls -Rl
}

# ----------------------

echo ""
echo "Building Flatpak"
echo ""

# Copy all the files to the root folder
copy_flatpak_files

# Change the app id to make it parallel installable with stable To Do.
modify_manifest
rename_files
change_module_type

# Print the new filesystem tree and manifest for debugging
print_new_files

echo ""
echo "Done"
echo ""
