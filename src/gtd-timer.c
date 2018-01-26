/* gtd-timer.c
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

#define G_LOG_DOMAIN "GtdTimer"

#include "gtd-timer.h"

#include <gio/gio.h>

struct _GtdTimer
{
  GtdObject           parent;

  guint               update_timeout_id;

  GDateTime          *current_day;

  GDBusProxy         *logind;
  GCancellable       *cancellable;
};

static gboolean      update_for_day_change                       (gpointer           user_data);

G_DEFINE_TYPE (GtdTimer, gtd_timer, GTD_TYPE_OBJECT)

enum
{
  UPDATE,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0, };

/*
 * Auxiliary methods
 */

static void
update_current_day (GtdTimer *self)
{
  g_autoptr (GDateTime) now;

  now = g_date_time_new_now_local ();

  if (g_date_time_get_year (now) != g_date_time_get_year (self->current_day) ||
      g_date_time_get_month (now) != g_date_time_get_month (self->current_day) ||
      g_date_time_get_day_of_month (now) != g_date_time_get_day_of_month (self->current_day))
    {
      g_clear_pointer (&self->current_day, g_date_time_unref);
      self->current_day = g_date_time_ref (now);

      g_signal_emit (self, signals[UPDATE], 0);
    }
}

static void
schedule_update_for_day_change (GtdTimer *self)
{
  g_autoptr (GDateTime) now;
  guint seconds_between;

  /* Remove the previous timeout if we came from resume */
  if (self->update_timeout_id > 0)
    {
      g_source_remove (self->update_timeout_id);
      self->update_timeout_id = 0;
    }

  now = g_date_time_new_now_local ();

  seconds_between = (24 - g_date_time_get_hour (now)) * 3600 +
                    (60 - g_date_time_get_minute (now)) * 60 +
                    (60 - g_date_time_get_seconds (now));

  self->update_timeout_id = g_timeout_add_seconds (seconds_between,
                                                   update_for_day_change,
                                                   self);
}

/*
 * Callbacks
 */

static void
logind_signal_received_cb (GDBusProxy  *logind,
                           const gchar *sender,
                           const gchar *signal,
                           GVariant    *params,
                           GtdTimer    *self)
{
  GVariant *child;
  gboolean resuming;

  if (!g_str_equal (signal, "PrepareForSleep"))
    return;

  child = g_variant_get_child_value (params, 0);
  resuming = !g_variant_get_boolean (child);

  /* Only emit :update when resuming */
  if (resuming)
    {
      /* Reschedule the daily timeout */
      update_current_day (self);
      schedule_update_for_day_change (self);
    }

  g_clear_pointer (&child, g_variant_unref);
}

static void
login_proxy_acquired_cb (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  GtdTimer *self;
  GError *error;

  self = GTD_TIMER (user_data);
  error = NULL;

  self->logind = g_dbus_proxy_new_for_bus_finish (res, &error);

  g_signal_connect (self->logind,
                    "g-signal",
                    G_CALLBACK (logind_signal_received_cb),
                    self);

  gtd_object_set_ready (GTD_OBJECT (self), TRUE);
}

static gboolean
update_for_day_change (gpointer user_data)
{
  GtdTimer *self = user_data;

  /* Remove it first */
  self->update_timeout_id = 0;

  /*
   * Because we can't rely on the current timeout,
   * reschedule it entirely.
   */
  update_current_day (self);
  schedule_update_for_day_change (self);

  return G_SOURCE_REMOVE;
}

/*
 * GObject overrides
 */
static void
gtd_timer_finalize (GObject *object)
{
  GtdTimer *self = (GtdTimer *)object;

  g_cancellable_cancel (self->cancellable);

  if (self->update_timeout_id > 0)
    {
      g_source_remove (self->update_timeout_id);
      self->update_timeout_id = 0;
    }

  g_clear_pointer (&self->current_day, g_date_time_unref);

  g_clear_object (&self->cancellable);
  g_clear_object (&self->logind);

  G_OBJECT_CLASS (gtd_timer_parent_class)->finalize (object);
}

static void
gtd_timer_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_timer_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_timer_class_init (GtdTimerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_timer_finalize;
  object_class->get_property = gtd_timer_get_property;
  object_class->set_property = gtd_timer_set_property;

  /**
   * GtdTimer:update:
   *
   * Emited when an update is required. This is emited usually
   * after a session resume, or a day change.
   */
  signals[UPDATE] = g_signal_new ("update",
                                  GTD_TYPE_TIMER,
                                  G_SIGNAL_RUN_LAST,
                                  0, NULL, NULL, NULL,
                                  G_TYPE_NONE,
                                  0);
}

static void
gtd_timer_init (GtdTimer *self)
{
  gtd_object_set_ready (GTD_OBJECT (self), FALSE);

  self->current_day = g_date_time_new_now_local ();
  self->cancellable = g_cancellable_new ();

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.freedesktop.login1",
                            "/org/freedesktop/login1",
                            "org.freedesktop.login1.Manager",
                            self->cancellable,
                            login_proxy_acquired_cb,
                            self);

  schedule_update_for_day_change (self);
}

GtdTimer*
gtd_timer_new (void)
{
  return g_object_new (GTD_TYPE_TIMER, NULL);
}
