#!/usr/bin/env python3

# __init__.py
#
# Copyright (C) 2016 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import gi

gi.require_version('Gtd',  '1.0')
gi.require_version('Peas', '1.0')

from gi.repository import Gio, GLib, GObject, Gtd, Gtk, Peas

try:
    import gettext
    gettext.bindtextdomain('gnome-todo')
    gettext.textdomain('gnome-todo')
    _ = gettext.gettext
except:
    _ = lambda s: s


class ScoreManager(GObject.Object):

    score = GObject.Property(type=int, default=0)

    __gsignals__ = {
        'score-added': (GObject.SignalFlags.RUN_FIRST, None, (int, Gtd.Task,)),
        'score-removed': (GObject.SignalFlags.RUN_FIRST, None, (int, Gtd.Task,))
    }

    def __init__(self):
        GObject.Object.__init__(self)

        manager = Gtd.Manager.get_default()

        manager.connect('list-added', self._setup_list)

        for tasklist in manager.get_task_lists():
            self._setup_list(manager, tasklist)

    def _setup_list(self, manager, tasklist):
        tasklist.connect('task-added', self._task_added)
        for task in tasklist.get_tasks():
            task.connect('notify::complete', self._task_complete)

    def _task_added(self, tasklist, task):
        task.connect('notify::complete', self._task_complete)

    def _task_complete(self, task, unused_data=None):
        task_value = 10 + task.get_priority() * 5

        if task.get_complete():
            self.score = self.score + task_value
            self.emit('score-added', self.score, task)
        else:
            self.score = self.score - task_value
            self.emit('score-removed', self.score, task)

class ScorePopover(Gtk.Popover):
    def __init__(self, button, manager):
        Gtk.Popover.__init__(self, relative_to=button)

        button.set_popover(self)

        self.manager = manager

        self._setup_listbox()
        self._setup_manager()

    def _setup_listbox(self):
        vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL,
                       spacing=6,
                       border_width=12)
        vbox.add(Gtk.Image.new_from_icon_name('face-embarrassed-symbolic',
                                              Gtk.IconSize.DIALOG))
        vbox.add(Gtk.Label(label=_("No task completed today")))
        vbox.show_all()

        self.listbox = Gtk.ListBox()
        self.listbox.set_selection_mode(Gtk.SelectionMode.NONE)
        self.listbox.set_placeholder(vbox)
        self.listbox.get_style_context().add_class('background')

        vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL,
                       spacing=6,
                       border_width=18)
        vbox.add(Gtk.Label(label='<b>' + _("Today") + '</b>',
                           use_markup=True,
                           hexpand=True,
                           xalign=0))
        vbox.add(self.listbox)
        vbox.show_all()

        self.add(vbox)

    def _setup_manager(self):
        self.manager.connect('score-added', self._score_added)
        self.manager.connect('score-removed', self._score_removed)

    def _score_added(self, manager, score, task):

        row = Gtk.ListBoxRow(border_width=6)

        row.add(Gtk.Label(label="<b>"+task.get_title()+"</b> completed",
                          use_markup=True,
                          hexpand=True,
                          xalign=0))
        row.show_all()

        self.listbox.add(row)

    def _score_removed(self, manager, score, task):

        row = Gtk.ListBoxRow(border_width=6)

        row.add(Gtk.Label(label="<b>"+task.get_title()+"</b> readded",
                          use_markup=True,
                          hexpand=True,
                          xalign=0))
        row.show_all()

        self.listbox.add(row)

class ScorePlugin(GObject.Object, Gtd.Activatable):

    preferences_panel = GObject.Property(type=Gtk.Widget, default=None)

    def __init__(self):
        GObject.Object.__init__(self)
        self.header_button = Gtk.MenuButton()
        self.header_button.set_halign(Gtk.Align.END)
        self.header_button.set_label('0')
        self.header_button.show_all()

        self.header_button.get_style_context().add_class('image-button')

        self.manager = ScoreManager()
        self.manager.connect('score-added', self._score_changed)
        self.manager.connect('score-removed', self._score_changed)

        self.popover = ScorePopover(self.header_button, self.manager)

    def _score_changed(self, manager, score, task):
        print(score)
        self.header_button.set_label(str(score))

    def do_activate(self):
        pass

    def do_deactivate(self):
        pass

    def do_get_header_widgets(self):
        return [self.header_button]

    def do_get_panels(self):
        return None

    def do_get_preferences_panel(self):
        return None

    def do_get_providers(self):
        return None
