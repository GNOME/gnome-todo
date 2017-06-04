FROM fedora:25
LABEL maintainer "waltervargas@linux.com"

ENV USER gnome

RUN useradd -m -u 1000 $USER

ADD . $HOME/gnome-todo
RUN chown -R $USER /home/$USER

ENV PACKAGES flatpak flatpak-builder git ostree
RUN dnf install -y $PACKAGES

USER $USER
WORKDIR $HOME/gnome-todo
RUN flatpak remote-add gnome-nightly --from https://sdk.gnome.org/gnome-nightly.flatpakrepo \
  && flatpak install gnome-nightly org.gnome.Sdk \
  && flatpak install gnome-nightly org.gnome.Platform
