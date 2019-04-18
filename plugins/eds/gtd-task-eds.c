/* gtd-task-eds.c
 *
 * Copyright (C) 2017 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#define G_LOG_DOMAIN "GtdTaskEds"

#include "gtd-task-eds.h"

struct _GtdTaskEds
{
  GtdTask             parent;

  ECalComponent      *component;

  gchar              *description;
};

G_DEFINE_TYPE (GtdTaskEds, gtd_task_eds, GTD_TYPE_TASK)

enum
{
  PROP_0,
  PROP_COMPONENT,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];


/*
 * Auxiliary methods
 */

static GDateTime*
convert_icaltime (const ICalTime *date)
{
  GDateTime *dt;

  if (!date)
    return NULL;

  dt = g_date_time_new_utc (i_cal_time_get_year (date),
                            i_cal_time_get_month (date),
                            i_cal_time_get_day (date),
                            i_cal_time_is_date (date) ? 0 : i_cal_time_get_hour (date),
                            i_cal_time_is_date (date) ? 0 : i_cal_time_get_minute (date),
                            i_cal_time_is_date (date) ? 0 : i_cal_time_get_second (date));

  return dt;
}

static void
set_description (GtdTaskEds  *self,
                 const gchar *description)
{
  ECalComponentText *text;
  GSList note;

  text = e_cal_component_text_new (description ? description : "", NULL);

  note.data = text;
  note.next = NULL;

  g_clear_pointer (&self->description, g_free);
  self->description = g_strdup (description);

  e_cal_component_set_descriptions (self->component, (description && *description) ? &note : NULL);

  e_cal_component_text_free (text);
}

static void
setup_description (GtdTaskEds *self)
{
  g_autofree gchar *desc = NULL;
  GSList *text_list;
  GSList *l;

  /* concatenates the multiple descriptions a task may have */
  text_list = e_cal_component_get_descriptions (self->component);

  for (l = text_list; l != NULL; l = l->next)
    {
      if (l->data != NULL)
        {
          ECalComponentText *text;
          gchar *carrier;

          text = l->data;

          if (desc)
            {
              carrier = g_strconcat (desc,
                                     "\n",
                                     e_cal_component_text_get_value (text),
                                     NULL);
              g_free (desc);
              desc = carrier;
            }
          else
            {
              desc = g_strdup (e_cal_component_text_get_value (text));
            }
        }
    }

  set_description (self, desc);

  g_slist_free_full (text_list, e_cal_component_text_free);
}


/*
 * GtdObject overrides
 */

static const gchar*
gtd_task_eds_get_uid (GtdObject *object)
{
  GtdTaskEds *self;
  const gchar *uid;

  g_return_val_if_fail (GTD_IS_TASK (object), NULL);

  self = GTD_TASK_EDS (object);

  if (self->component)
    uid = e_cal_component_get_uid (self->component);
  else
    uid = NULL;

  return uid;
}

static void
gtd_task_eds_set_uid (GtdObject   *object,
                      const gchar *uid)
{
  GtdTaskEds *self;
  const gchar *current_uid;

  g_return_if_fail (GTD_IS_TASK (object));

  self = GTD_TASK_EDS (object);

  if (!self->component)
    return;

  current_uid = e_cal_component_get_uid (self->component);

  if (g_strcmp0 (current_uid, uid) != 0)
    {
      e_cal_component_set_uid (self->component, uid);

      g_object_notify (G_OBJECT (object), "uid");
    }
}


/*
 * GtdTask overrides
 */

static gboolean
gtd_task_eds_get_complete (GtdTask *task)
{
  ICalPropertyStatus status;
  GtdTaskEds *self;
  gboolean completed;

  g_return_val_if_fail (GTD_IS_TASK_EDS (task), FALSE);

  self = GTD_TASK_EDS (task);

  status = e_cal_component_get_status (self->component);
  completed = status == I_CAL_STATUS_COMPLETED;

  return completed;
}

static void
gtd_task_eds_set_complete (GtdTask  *task,
                           gboolean  complete)
{
  ICalPropertyStatus status;
  ICalTime *dt;
  GtdTaskEds *self;
  gint percent;

  self = GTD_TASK_EDS (task);

  if (complete)
    {
      g_autoptr (GDateTime) now = g_date_time_new_now_utc ();

      percent = 100;
      status = ICAL_STATUS_COMPLETED;

      dt = i_cal_time_null_time ();
      i_cal_time_set_date (dt,
                           g_date_time_get_year (now),
                           g_date_time_get_month (now),
                           g_date_time_get_day_of_month (now));
      i_cal_time_set_time (dt,
                           g_date_time_get_hour (now),
                           g_date_time_get_minute (now),
                           g_date_time_get_seconds (now));
      i_cal_time_set_timezone (dt, i_cal_timezone_get_utc_timezone ());

      /* convert timezone
       *
       * FIXME: This does not do anything until we have an ical
       * timezone associated with the task
       */
      i_cal_timezone_convert_time (dt, NULL, i_cal_timezone_get_utc_timezone ());
    }
  else
    {
      dt = NULL;
      percent = 0;
      status = I_CAL_STATUS_NEEDSACTION;
    }

  e_cal_component_set_percent_complete (self->component, percent);
  e_cal_component_set_status (self->component, status);
  e_cal_component_set_completed (self->component, dt);

  g_clear_object (&dt);
}

static GDateTime*
gtd_task_eds_get_creation_date (GtdTask *task)
{
  ICalTime *idt;
  GtdTaskEds *self;
  GDateTime *dt;

  self = GTD_TASK_EDS (task);
  dt = NULL;

  idt = e_cal_component_get_created (self->component);

  if (idt)
    dt = convert_icaltime (idt);

  g_clear_object (&idt);

  return dt;
}

static void
gtd_task_eds_set_creation_date (GtdTask   *task,
                                GDateTime *dt)
{
  g_assert_not_reached ();
}

static const gchar*
gtd_task_eds_get_description (GtdTask *task)
{
  GtdTaskEds *self = GTD_TASK_EDS (task);

  return self->description ? self->description : "";
}

static void
gtd_task_eds_set_description (GtdTask     *task,
                              const gchar *description)
{
  set_description (GTD_TASK_EDS (task), description);
}

static GDateTime*
gtd_task_eds_get_due_date (GtdTask *task)
{
  ECalComponentDateTime *comp_dt;
  GtdTaskEds *self;
  GDateTime *date;

  g_return_val_if_fail (GTD_IS_TASK_EDS (task), NULL);

  self = GTD_TASK_EDS (task);

  comp_dt = e_cal_component_get_due (self->component);
  if (!comp_dt)
    return NULL;

  date = convert_icaltime (e_cal_component_datetime_get_value (comp_dt));
  e_cal_component_datetime_free (comp_dt);

  return date;
}

static void
gtd_task_eds_set_due_date (GtdTask   *task,
                           GDateTime *dt)
{
  GtdTaskEds *self;
  GDateTime *current_dt;

  g_assert (GTD_IS_TASK_EDS (task));

  self = GTD_TASK_EDS (task);

  current_dt = gtd_task_get_due_date (task);

  if (dt != current_dt)
    {
      ECalComponentDateTime *comp_dt;
      ICalTime *idt;

      comp_dt = NULL;
      idt = NULL;

      if (!current_dt ||
          (current_dt &&
           dt &&
           g_date_time_compare (current_dt, dt) != 0))
        {
          idt = i_cal_time_null_time ();

          g_date_time_ref (dt);

          /* Copy the given dt */
          i_cal_time_set_date (idt,
                               g_date_time_get_year (dt),
                               g_date_time_get_month (dt),
                               g_date_time_get_day_of_month (dt));
          i_cal_time_set_time (idt,
                               g_date_time_get_hour (dt),
                               g_date_time_get_minute (dt),
                               g_date_time_get_seconds (dt));
          i_cal_time_set_is_date (idt,
                          i_cal_time_get_hour (idt) == 0 &&
                          i_cal_time_get_minute (idt) == 0 &&
                          i_cal_time_get_second (idt) == 0);

          comp_dt = e_cal_component_datetime_new_take (idt, g_strdup ("UTC"));

          e_cal_component_set_due (self->component, comp_dt);

          e_cal_component_datetime_free (comp_dt);

          g_date_time_unref (dt);
        }
      else if (!dt)
        {
          e_cal_component_set_due (self->component, NULL);
        }
    }

  g_clear_pointer (&current_dt, g_date_time_unref);
}

static gint32
gtd_task_eds_get_priority (GtdTask *task)
{
  GtdTaskEds *self;

  g_assert (GTD_IS_TASK_EDS (task));

  self = GTD_TASK_EDS (task);

  return e_cal_component_get_priority (self->component);
}

static void
gtd_task_eds_set_priority (GtdTask *task,
                           gint32   priority)
{
  GtdTaskEds *self;

  g_assert (GTD_IS_TASK_EDS (task));

  self = GTD_TASK_EDS (task);

  e_cal_component_set_priority (self->component, priority);
}

static const gchar*
gtd_task_eds_get_title (GtdTask *task)
{
  GtdTaskEds *self;

  g_return_val_if_fail (GTD_IS_TASK_EDS (task), NULL);

  self = GTD_TASK_EDS (task);

  return i_cal_component_get_summary (e_cal_component_get_icalcomponent (self->component));
}

static void
gtd_task_eds_set_title (GtdTask     *task,
                        const gchar *title)
{
  ECalComponentText *new_summary;
  GtdTaskEds *self;

  g_return_if_fail (GTD_IS_TASK_EDS (task));
  g_return_if_fail (g_utf8_validate (title, -1, NULL));

  self = GTD_TASK_EDS (task);

  new_summary = e_cal_component_text_new (title, NULL);

  e_cal_component_set_summary (self->component, new_summary);

  e_cal_component_text_free (new_summary);
}


static void
gtd_task_eds_subtask_added (GtdTask *task,
                            GtdTask *subtask)
{
  g_autoptr (GList) subtasks = NULL;
  const gchar *uid;
  ECalComponent *comp;
  ICalComponent *ical_comp;
  ICalProperty *property;
  GtdTaskEds *subtask_self;
  GtdTaskEds *self;

  self = GTD_TASK_EDS (task);
  subtask_self = GTD_TASK_EDS (subtask);
  subtasks = gtd_task_get_subtasks (task);

  /* Hook with parent's :subtask_added */
  GTD_TASK_CLASS (gtd_task_eds_parent_class)->subtask_added (task, subtask);

  uid = e_cal_component_get_uid (self->component);
  comp = subtask_self->component;
  ical_comp = e_cal_component_get_icalcomponent (comp);
  property = i_cal_component_get_first_property (ical_comp, I_CAL_RELATEDTO_PROPERTY);

  if (property)
    i_cal_property_set_relatedto (property, uid);
  else
    i_cal_component_take_property (ical_comp, i_cal_property_new_relatedto (uid));

  g_clear_object (&property);
}

static void
gtd_task_eds_subtask_removed (GtdTask *task,
                              GtdTask *subtask)
{
  g_autoptr (GList) subtasks = NULL;
  ICalComponent *ical_comp;
  ICalProperty *property;
  GtdTaskEds *subtask_self;

  subtask_self = GTD_TASK_EDS (subtask);
  subtasks = gtd_task_get_subtasks (task);

  /* Hook with parent's :subtask_removed */
  GTD_TASK_CLASS (gtd_task_eds_parent_class)->subtask_removed (task, subtask);

  /* Remove the parent link from the subtask's component */
  ical_comp = e_cal_component_get_icalcomponent (subtask_self->component);
  property = i_cal_component_get_first_property (ical_comp, I_CAL_RELATEDTO_PROPERTY);

  if (!property)
    return;

  i_cal_component_remove_property (ical_comp, property);
  g_object_unref (property);
}


/*
 * GObject overrides
 */

static void
gtd_task_eds_finalize (GObject *object)
{
  GtdTaskEds *self = (GtdTaskEds *)object;

  g_clear_object (&self->component);

  G_OBJECT_CLASS (gtd_task_eds_parent_class)->finalize (object);
}

static void
gtd_task_eds_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  GtdTaskEds *self = GTD_TASK_EDS (object);

  switch (prop_id)
    {
    case PROP_COMPONENT:
      g_value_set_object (value, self->component);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_eds_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  GtdTaskEds *self = GTD_TASK_EDS (object);

  switch (prop_id)
    {
    case PROP_COMPONENT:
      if (g_set_object (&self->component, g_value_get_object (value)))
        {
          setup_description (self);
          g_object_notify_by_pspec (object, properties[PROP_COMPONENT]);
        }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_eds_class_init (GtdTaskEdsClass *klass)
{
  GtdObjectClass *gtd_object_class = GTD_OBJECT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtdTaskClass *task_class = GTD_TASK_CLASS (klass);

  object_class->finalize = gtd_task_eds_finalize;
  object_class->get_property = gtd_task_eds_get_property;
  object_class->set_property = gtd_task_eds_set_property;

  task_class->get_complete = gtd_task_eds_get_complete;
  task_class->set_complete = gtd_task_eds_set_complete;
  task_class->get_creation_date = gtd_task_eds_get_creation_date;
  task_class->set_creation_date = gtd_task_eds_set_creation_date;
  task_class->get_description = gtd_task_eds_get_description;
  task_class->set_description = gtd_task_eds_set_description;
  task_class->get_due_date = gtd_task_eds_get_due_date;
  task_class->set_due_date = gtd_task_eds_set_due_date;
  task_class->get_priority = gtd_task_eds_get_priority;
  task_class->set_priority = gtd_task_eds_set_priority;
  task_class->get_title = gtd_task_eds_get_title;
  task_class->set_title = gtd_task_eds_set_title;
  task_class->subtask_added = gtd_task_eds_subtask_added;
  task_class->subtask_removed = gtd_task_eds_subtask_removed;

  gtd_object_class->get_uid = gtd_task_eds_get_uid;
  gtd_object_class->set_uid = gtd_task_eds_set_uid;

  properties[PROP_COMPONENT] = g_param_spec_object ("component",
                                                    "Component",
                                                    "Component",
                                                    E_TYPE_CAL_COMPONENT,
                                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
gtd_task_eds_init (GtdTaskEds *self)
{
}

GtdTask*
gtd_task_eds_new (ECalComponent *component)
{
  return g_object_new (GTD_TYPE_TASK_EDS,
                       "component", component,
                       NULL);
}

ECalComponent*
gtd_task_eds_get_component (GtdTaskEds *self)
{
  g_return_val_if_fail (GTD_IS_TASK_EDS (self), NULL);

  return self->component;
}

void
gtd_task_eds_set_component (GtdTaskEds    *self,
                            ECalComponent *component)
{
  GObject *object;

  g_return_if_fail (GTD_IS_TASK_EDS (self));
  g_return_if_fail (E_IS_CAL_COMPONENT (component));

  if (!g_set_object (&self->component, component))
    return;

  object = G_OBJECT (self);

  setup_description (self);

  g_object_notify (object, "complete");
  g_object_notify (object, "creation-date");
  g_object_notify (object, "description");
  g_object_notify (object, "due-date");
  g_object_notify (object, "priority");
  g_object_notify (object, "title");
  g_object_notify_by_pspec (object, properties[PROP_COMPONENT]);
}
