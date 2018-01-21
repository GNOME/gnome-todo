/* gtd-task-list-eds.c
 *
 * Copyright (C) 2015 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "GtdTaskListEds"

#include "gtd-task-eds.h"
#include "gtd-task-list-eds.h"

#include <glib/gi18n.h>

struct _GtdTaskListEds
{
  GtdTaskList         parent;

  ESource            *source;

  GPtrArray           *pending_subtasks;

  GCancellable       *cancellable;
};

typedef struct
{
  GtdTask            *child;
  gchar              *parent_uid;
} PendingSubtaskData;

G_DEFINE_TYPE (GtdTaskListEds, gtd_task_list_eds, GTD_TYPE_TASK_LIST)

enum {
  PROP_0,
  PROP_SOURCE,
  N_PROPS
};


/*
 * Auxiliary methods
 */

static PendingSubtaskData*
pending_subtask_data_new (GtdTask     *child,
                          const gchar *parent_uid)
{
  PendingSubtaskData *data;

  data = g_new0 (PendingSubtaskData, 1);
  data->child = child;
  data->parent_uid = g_strdup (parent_uid);

  return data;
}

static void
pending_subtask_data_free (PendingSubtaskData *data)
{
  g_free (data->parent_uid);
  g_free (data);
}

static void
setup_parent_task (GtdTaskListEds *self,
                   GtdTask        *task)
{
  ECalComponent *component;
  icalcomponent *ical_comp;
  icalproperty *property;
  GtdTask *parent_task;
  const gchar *parent_uid;

  component = gtd_task_eds_get_component (GTD_TASK_EDS (task));
  ical_comp = e_cal_component_get_icalcomponent (component);
  property = icalcomponent_get_first_property (ical_comp, ICAL_RELATEDTO_PROPERTY);

  if (!property)
    return;

  parent_uid = icalproperty_get_relatedto (property);
  parent_task = gtd_task_list_get_task_by_id (GTD_TASK_LIST (self), parent_uid);

  if (parent_task)
    {
      gtd_task_add_subtask (parent_task, task);
    }
  else
    {
      PendingSubtaskData *data;

      data = pending_subtask_data_new (task, parent_uid);

      g_ptr_array_add (self->pending_subtasks, data);
    }
}

static void
process_pending_subtasks (GtdTaskListEds *self,
                          GtdTask        *task)
{
  const gchar *uid;
  guint i;

  uid = gtd_object_get_uid (GTD_OBJECT (task));

  for (i = 0; i < self->pending_subtasks->len; i++)
    {
      PendingSubtaskData *data;

      data = g_ptr_array_index (self->pending_subtasks, i);

      if (g_strcmp0 (uid, data->parent_uid) == 0)
        {
          gtd_task_add_subtask (task, data->child);
          g_ptr_array_remove (self->pending_subtasks, data);
          i--;
        }
    }
}


static void
source_removable_changed (GtdTaskListEds *list)
{
  gtd_task_list_set_is_removable (GTD_TASK_LIST (list),
                                  e_source_get_removable (list->source) ||
                                  e_source_get_remote_deletable (list->source));
}

static void
save_task_list_finished_cb (GObject      *source,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  GtdTaskListEds *list;
  GError *error;

  list = user_data;
  error = NULL;

  gtd_object_set_ready (GTD_OBJECT (list), TRUE);

  e_source_write_finish (E_SOURCE (source),
                         result,
                         &error);

  if (error)
    {
      g_warning ("%s: %s: %s",
                 G_STRFUNC,
                 "Error saving task list",
                 error->message);
      g_clear_error (&error);
    }
}

static void
save_task_list (GtdTaskListEds *list)
{
  if (e_source_get_writable (list->source))
    {
      if (!list->cancellable)
        list->cancellable = g_cancellable_new ();

      gtd_object_set_ready (GTD_OBJECT (list), FALSE);

      e_source_write (list->source,
                      list->cancellable,
                      save_task_list_finished_cb,
                      list);
    }
}
static gboolean
color_to_string (GBinding     *binding,
                 const GValue *from_value,
                 GValue       *to_value,
                 gpointer      user_data)
{
  GdkRGBA *color;
  gchar *color_str;

  color = g_value_get_boxed (from_value);
  color_str = gdk_rgba_to_string (color);

  g_value_set_string (to_value, color_str);

  g_free (color_str);

  return TRUE;
}

static gboolean
string_to_color (GBinding     *binding,
                 const GValue *from_value,
                 GValue       *to_value,
                 gpointer      user_data)
{
  GdkRGBA color;

  if (!gdk_rgba_parse (&color, g_value_get_string (from_value)))
    gdk_rgba_parse (&color, "#ffffff"); /* calendar default colour */

  g_value_set_boxed (to_value, &color);

  return TRUE;
}


/*
 * GtdTaskList overrides
 */
static void
gtd_task_list_eds_task_added (GtdTaskList *list,
                              GtdTask     *task)
{
  GtdTaskListEds *self = GTD_TASK_LIST_EDS (list);

  process_pending_subtasks (self, task);
  setup_parent_task (self, task);
}


/*
 * GObject overrides
 */

static void
gtd_task_list_eds_finalize (GObject *object)
{
  GtdTaskListEds *self = GTD_TASK_LIST_EDS (object);

  g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);
  g_clear_object (&self->source);

  G_OBJECT_CLASS (gtd_task_list_eds_parent_class)->finalize (object);
}

static void
gtd_task_list_eds_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GtdTaskListEds *self = GTD_TASK_LIST_EDS (object);

  switch (prop_id)
    {
    case PROP_SOURCE:
      g_value_set_object (value, self->source);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_list_eds_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GtdTaskListEds *self = GTD_TASK_LIST_EDS (object);

  switch (prop_id)
    {
    case PROP_SOURCE:
      gtd_task_list_eds_set_source (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_list_eds_class_init (GtdTaskListEdsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtdTaskListClass *task_list_class = GTD_TASK_LIST_CLASS (klass);

  task_list_class->task_added = gtd_task_list_eds_task_added;

  object_class->finalize = gtd_task_list_eds_finalize;
  object_class->get_property = gtd_task_list_eds_get_property;
  object_class->set_property = gtd_task_list_eds_set_property;

  /**
   * GtdTaskListEds::source:
   *
   * The #ESource of this #GtdTaskListEds
   */
  g_object_class_install_property (object_class,
                                   PROP_SOURCE,
                                   g_param_spec_object ("source",
                                                        "ESource of this list",
                                                        "The ESource of this list",
                                                        E_TYPE_SOURCE,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
gtd_task_list_eds_init (GtdTaskListEds *self)
{
  self->pending_subtasks = g_ptr_array_new_with_free_func ((GDestroyNotify) pending_subtask_data_free);
}

GtdTaskListEds*
gtd_task_list_eds_new (GtdProvider *provider,
                       ESource     *source)
{
  return g_object_new (GTD_TYPE_TASK_LIST_EDS,
                       "provider", provider,
                       "source", source,
                       NULL);
}

ESource*
gtd_task_list_eds_get_source (GtdTaskListEds *list)
{
  g_return_val_if_fail (GTD_IS_TASK_LIST_EDS (list), NULL);

  return list->source;
}

void
gtd_task_list_eds_set_source (GtdTaskListEds *list,
                              ESource        *source)
{
  g_return_if_fail (GTD_IS_TASK_LIST_EDS (list));

  if (g_set_object (&list->source, source))
    {
      ESourceSelectable *selectable;
      GdkRGBA color;

      /* Setup color */
      selectable = E_SOURCE_SELECTABLE (e_source_get_extension (source, E_SOURCE_EXTENSION_TASK_LIST));

      if (!gdk_rgba_parse (&color, e_source_selectable_get_color (selectable)))
        gdk_rgba_parse (&color, "#ffffff"); /* calendar default color */

      gtd_task_list_set_color (GTD_TASK_LIST (list), &color);

      g_object_bind_property_full (list,
                                   "color",
                                   selectable,
                                   "color",
                                   G_BINDING_BIDIRECTIONAL,
                                   color_to_string,
                                   string_to_color,
                                   list,
                                   NULL);

      /* Setup tasklist name */
      gtd_task_list_set_name (GTD_TASK_LIST (list), e_source_get_display_name (source));

      g_object_bind_property (source,
                              "display-name",
                              list,
                              "name",
                              G_BINDING_BIDIRECTIONAL);

      /* Save the task list every time something changes */
      g_signal_connect_swapped (source,
                                "notify",
                                G_CALLBACK (save_task_list),
                                list);

      /* Update ::is-removable property */
      gtd_task_list_set_is_removable (GTD_TASK_LIST (list),
                                      e_source_get_removable (source) ||
                                      e_source_get_remote_deletable (source));

      g_signal_connect_swapped (source,
                                "notify::removable",
                                G_CALLBACK (source_removable_changed),
                                list);

      g_signal_connect_swapped (source,
                                "notify::remote-deletable",
                                G_CALLBACK (source_removable_changed),
                                list);

      g_object_notify (G_OBJECT (list), "source");
    }
}
