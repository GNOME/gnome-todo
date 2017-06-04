def base_image = "waltervargas/gnome-todo"

node {
  checkout scm

  stage("Flatpak build"){

    sh """PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/local/bin
/usr/local/bin/flatpak-builder --disable-rofiles-fuse --force-clean --repo=repo dist org.gnome.Todo.Test.json
"""
  }

  stage("Faltpak tests"){
    echo "tests"
    //sh 'flatpak-builder --run dist org.gnome.Todo.Test.json gnome-desktop-testing-runner gnome-todo'
  }
}
