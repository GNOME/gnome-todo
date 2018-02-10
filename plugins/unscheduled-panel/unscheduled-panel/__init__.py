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


class UnscheduledPanel(Gtk.Box, Gtd.Panel):

    menu = GObject.Property(type=Gio.Menu, default=None)
    name = GObject.Property(type=str, default="unscheduled-panel")
    title = GObject.Property(type=str, default=_("Unscheduled"))

    def __init__(self):
        Gtk.Box.__init__(self)

        manager = Gtd.Manager.get_default()
        manager.connect('list-added', self._count_tasks)
        manager.connect('list-changed', self._count_tasks)
        manager.connect('list-removed', self._count_tasks)

        self.task_counter = 0

        self.view = Gtd.TaskListView(hexpand=True,
                                     vexpand=True)
        self.view.set_show_list_name(True)
        self.view.set_handle_subtasks(False)

        self.menu = Gio.Menu()
        self.menu.append(_("Clear completed tasks…"), "list.clear-completed-tasks")

        self.add(self.view)
        self.show_all()

        self._count_tasks()

    def _count_tasks(self, unused_0=None, unused_1=None):

        previous_task_counter = self.task_counter
        self.task_counter = 0

        manager = Gtd.Manager.get_default()
        current_tasks = []

        for tasklist in manager.get_task_lists():
            for task in tasklist.get_tasks():

                if not task.get_due_date() is None:
                    continue

                current_tasks.append(task)

                # Update the counter
                if not task.get_complete():
                    self.task_counter += 1

        self.view.set_list(current_tasks)

        if previous_task_counter != self.task_counter:
            self.notify("title")

    def do_get_header_widgets(self):
        return None

    def do_get_menu(self):
        return self.menu

    def do_get_panel_name(self):
        return "unscheduled-panel"

    def do_get_panel_title(self):
        if self.task_counter == 0:
            # Translators: 'Unscheduled' as in 'Unscheduled tasks'
            return _("Unscheduled")
        else:
            # Translators: 'Unscheduled' as in 'Unscheduled tasks'
            return _("Unscheduled (%d)" % self.task_counter)

class UnscheduledPanelPlugin(GObject.Object, Gtd.Activatable):

    preferences_panel = GObject.Property(type=Gtk.Widget, default=None)

    def __init__(self):
        GObject.Object.__init__(self)

        self.panel = UnscheduledPanel()

    def do_activate(self):
        pass

    def do_deactivate(self):
        pass

    def do_get_header_widgets(self):
        return None

    def do_get_panels(self):
        return [self.panel]

    def do_get_preferences_panel(self):
        return None

    def do_get_providers(self):
        return None
