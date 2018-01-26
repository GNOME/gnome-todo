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
convert_icaltime (const icaltimetype *date)
{
  GDateTime *dt;

  if (!date)
    return NULL;

  dt = g_date_time_new_utc (date->year,
                            date->month,
                            date->day,
                            date->is_date ? 0 : date->hour,
                            date->is_date ? 0 : date->minute,
                            date->is_date ? 0 : date->second);

  return dt;
}

static void
set_description (GtdTaskEds  *self,
                 const gchar *description)
{
  ECalComponentText text;
  GSList note;

  text.value = description && *description ? description : "";
  text.altrep = NULL;

  note.data = &text;
  note.next = NULL;

  g_clear_pointer (&self->description, g_free);
  self->description = g_strdup (description);

  e_cal_component_set_description_list (self->component, &note);
}

static void
setup_description (GtdTaskEds *self)
{
  g_autofree gchar *desc = NULL;
  GSList *text_list;
  GSList *l;

  /* concatenates the multiple descriptions a task may have */
  e_cal_component_get_description_list (self->component, &text_list);

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
                                     text->value,
                                     NULL);
              g_free (desc);
              desc = carrier;
            }
          else
            {
              desc = g_strdup (text->value);
            }
        }
    }

  set_description (self, desc);

  e_cal_component_free_text_list (text_list);
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
    e_cal_component_get_uid (self->component, &uid);
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

  e_cal_component_get_uid (self->component, &current_uid);

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
  GtdTaskEds *self;
  icaltimetype *dt;
  gboolean completed;

  g_return_val_if_fail (GTD_IS_TASK_EDS (task), FALSE);

  self = GTD_TASK_EDS (task);

  e_cal_component_get_completed (self->component, &dt);
  completed = (dt != NULL);

  g_clear_pointer (&dt, e_cal_component_free_icaltimetype);

  return completed;
}

static void
gtd_task_eds_set_complete (GtdTask  *task,
                           gboolean  complete)
{
  icalproperty_status status;
  icaltimetype *dt;
  GtdTaskEds *self;
  gint percent;

  self = GTD_TASK_EDS (task);

  if (complete)
    {
      GDateTime *now = g_date_time_new_now_utc ();

      percent = 100;
      status = ICAL_STATUS_COMPLETED;

      dt = g_new0 (icaltimetype, 1);
      dt->year = g_date_time_get_year (now);
      dt->month = g_date_time_get_month (now);
      dt->day = g_date_time_get_day_of_month (now);
      dt->hour = g_date_time_get_hour (now);
      dt->minute = g_date_time_get_minute (now);
      dt->second = g_date_time_get_seconds (now);
      dt->zone = icaltimezone_get_utc_timezone ();

      /* convert timezone
       *
       * FIXME: This does not do anything until we have an ical
       * timezone associated with the task
       */
      icaltimezone_convert_time (dt,
                                 NULL,
                                 icaltimezone_get_utc_timezone ());
      g_date_time_unref (now);
    }
  else
    {
      dt = NULL;
      percent = 0;
      status = ICAL_STATUS_NEEDSACTION;
    }

  e_cal_component_set_percent_as_int (self->component, percent);
  e_cal_component_set_status (self->component, status);
  e_cal_component_set_completed (self->component, dt);

  if (dt)
    e_cal_component_free_icaltimetype (dt);
}

static GDateTime*
gtd_task_eds_get_creation_date (GtdTask *task)
{
  icaltimetype *idt;
  GtdTaskEds *self;
  GDateTime *dt;

  self = GTD_TASK_EDS (task);
  idt = NULL;
  dt = NULL;

  e_cal_component_get_created (self->component, &idt);

  if (idt)
    dt = convert_icaltime (idt);

  g_clear_pointer (&idt, e_cal_component_free_icaltimetype);

  return dt;
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
  ECalComponentDateTime comp_dt;
  GtdTaskEds *self;
  GDateTime *date;

  g_return_val_if_fail (GTD_IS_TASK_EDS (task), NULL);

  self = GTD_TASK_EDS (task);

  e_cal_component_get_due (self->component, &comp_dt);

  date = convert_icaltime (comp_dt.value);
  e_cal_component_free_datetime (&comp_dt);

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
      ECalComponentDateTime comp_dt;
      icaltimetype *idt;

      comp_dt.value = NULL;
      comp_dt.tzid = NULL;
      idt = NULL;

      if (!current_dt ||
          (current_dt &&
           dt &&
           g_date_time_compare (current_dt, dt) != 0))
        {
          idt = g_new0 (icaltimetype, 1);

          g_date_time_ref (dt);

          /* Copy the given dt */
          idt->year = g_date_time_get_year (dt);
          idt->month = g_date_time_get_month (dt);
          idt->day = g_date_time_get_day_of_month (dt);
          idt->hour = g_date_time_get_hour (dt);
          idt->minute = g_date_time_get_minute (dt);
          idt->second = g_date_time_get_seconds (dt);
          idt->is_date = (idt->hour == 0 &&
                          idt->minute == 0 &&
                          idt->second == 0);

          comp_dt.tzid = g_strdup ("UTC");

          comp_dt.value = idt;

          e_cal_component_set_due (self->component, &comp_dt);

          e_cal_component_free_datetime (&comp_dt);

          g_date_time_unref (dt);
        }
      else if (!dt)
        {
          idt = NULL;
          comp_dt.tzid = NULL;

          e_cal_component_set_due (self->component, NULL);
        }
    }

  g_clear_pointer (&current_dt, g_date_time_unref);
}

static gint32
gtd_task_eds_get_priority (GtdTask *task)
{
  g_autofree gint *priority = NULL;
  GtdTaskEds *self;

  g_assert (GTD_IS_TASK_EDS (task));

  self = GTD_TASK_EDS (task);

  e_cal_component_get_priority (self->component, &priority);

  if (!priority)
    return -1;

  return *priority;
}

static void
gtd_task_eds_set_priority (GtdTask *task,
                           gint32   priority)
{
  GtdTaskEds *self;

  g_assert (GTD_IS_TASK_EDS (task));

  self = GTD_TASK_EDS (task);

  e_cal_component_set_priority (self->component, &priority);
}

static const gchar*
gtd_task_eds_get_title (GtdTask *task)
{
  ECalComponentText summary;
  GtdTaskEds *self;

  g_return_val_if_fail (GTD_IS_TASK_EDS (task), NULL);

  self = GTD_TASK_EDS (task);

  e_cal_component_get_summary (self->component, &summary);

  return summary.value;
}

static void
gtd_task_eds_set_title (GtdTask     *task,
                        const gchar *title)
{
  ECalComponentText new_summary;
  GtdTaskEds *self;

  g_return_if_fail (GTD_IS_TASK_EDS (task));
  g_return_if_fail (g_utf8_validate (title, -1, NULL));

  self = GTD_TASK_EDS (task);

  new_summary.value = title;
  new_summary.altrep = NULL;

  e_cal_component_set_summary (self->component, &new_summary);
}


static void
gtd_task_eds_subtask_added (GtdTask *task,
                            GtdTask *subtask)
{
  g_autoptr (GList) subtasks = NULL;
  ECalComponentId *id;
  ECalComponent *comp;
  icalcomponent *ical_comp;
  icalproperty *property;
  GtdTaskEds *subtask_self;
  GtdTaskEds *self;

  self = GTD_TASK_EDS (task);
  subtask_self = GTD_TASK_EDS (subtask);
  subtasks = gtd_task_get_subtasks (task);

  /* Hook with parent's :subtask_added */
  GTD_TASK_CLASS (gtd_task_eds_parent_class)->subtask_added (task, subtask);

  id = e_cal_component_get_id (self->component);
  comp = subtask_self->component;
  ical_comp = e_cal_component_get_icalcomponent (comp);
  property = icalcomponent_get_first_property (ical_comp, ICAL_RELATEDTO_PROPERTY);

  if (property)
    icalproperty_set_relatedto (property, id->uid);
  else
    icalcomponent_add_property (ical_comp, icalproperty_new_relatedto (id->uid));

  e_cal_component_free_id (id);
}

static void
gtd_task_eds_subtask_removed (GtdTask *task,
                              GtdTask *subtask)
{
  g_autoptr (GList) subtasks = NULL;
  icalcomponent *ical_comp;
  icalproperty *property;
  GtdTaskEds *subtask_self;

  subtask_self = GTD_TASK_EDS (subtask);
  subtasks = gtd_task_get_subtasks (task);

  /* Hook with parent's :subtask_removed */
  GTD_TASK_CLASS (gtd_task_eds_parent_class)->subtask_removed (task, subtask);

  /* Remove the parent link from the subtask's component */
  ical_comp = e_cal_component_get_icalcomponent (subtask_self->component);
  property = icalcomponent_get_first_property (ical_comp, ICAL_RELATEDTO_PROPERTY);

  if (!property)
    return;

  icalcomponent_remove_property (ical_comp, property);
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
                                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

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
