def base_image = "waltervargas/gnome-todo"

node {
  checkout scm

  stage("Flatpak build"){
    sh 'flatpak-builder --repo=repo dist org.gnome.Todo.Test.json'
  }

  stage("Faltpak tests"){
    sh 'flatpak-builder --run dist org.gnome.Todo.Test.json gnome-desktop-testing-runner gnome-todo'
  }

}