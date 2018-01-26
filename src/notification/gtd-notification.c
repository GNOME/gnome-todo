/* gtd-notification.c
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

#define G_LOG_DOMAIN "GtdNotification"

#include "gtd-notification.h"
#include "gtd-object.h"

#include <glib/gi18n.h>

/**
 * SECTION:gtd-notification
 * @short_description:a notification with actions
 * @title:GtdNotification
 * @image:notification.png
 * @stability:Stable
 *
 * The #GtdNotification represents a notification shown at the top of
 * the window. The action can have a primary action that is called when
 * the notification is gone.
 *
 * Optionally, the notification may have a secondary action (see gtd_notification_set_secondary_action())
 * shown as a button.
 *
 * The notification may also have a timeout, which is how long the notification
 * is displayed. By default, the timeout is 7.5 seconds.
 *
 * Example:
 * |[
 * GtdNotification *notification;
 *
 * notification = gtd_notification_new ("Something happened!", 7500);
 *
 * gtd_notification_set_primary_action (notification,
 *                                      called_when_notification_is_closed,
 *                                      self);
 *
 * gtd_notification_set_secondary_action (notification,
 *                                        "Details",
 *                                        show_details,
 *                                        self);
 * [...]
 *
 * gtd_window_send_notification (window, notification);
 * ]|
 */

typedef struct
{
  gchar              *text;

  gdouble             timeout;
  gint                timeout_id;

  GtdNotificationActionFunc primary_action;
  gboolean            has_primary_action;
  gpointer            primary_action_data;

  GtdNotificationActionFunc secondary_action;
  gboolean            has_secondary_action;
  gpointer            secondary_action_data;
  gchar              *secondary_action_name;
} GtdNotificationPrivate;

struct _GtdNotification
{
  GtdObject               parent;

  /*< private >*/
  GtdNotificationPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE (GtdNotification, gtd_notification, GTD_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_HAS_PRIMARY_ACTION,
  PROP_HAS_SECONDARY_ACTION,
  PROP_SECONDARY_ACTION_NAME,
  PROP_TEXT,
  PROP_TIMEOUT,
  LAST_PROP
};

enum
{
  EXECUTED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0, };

static gboolean
execute_action_cb (GtdNotification *notification)
{
  GtdNotificationPrivate *priv = notification->priv;

  priv->timeout_id = 0;

  gtd_notification_execute_primary_action (notification);

  return G_SOURCE_REMOVE;
}

static void
gtd_notification_finalize (GObject *object)
{
  GtdNotification *self = (GtdNotification *)object;
  GtdNotificationPrivate *priv = gtd_notification_get_instance_private (self);

  if (priv->timeout_id > 0)
    g_source_remove (priv->timeout_id);

  g_clear_pointer (&priv->secondary_action_name, g_free);
  g_clear_pointer (&priv->text, g_free);

  G_OBJECT_CLASS (gtd_notification_parent_class)->finalize (object);
}

static void
gtd_notification_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GtdNotification *self = GTD_NOTIFICATION (object);

  switch (prop_id)
    {
    case PROP_HAS_PRIMARY_ACTION:
      g_value_set_boolean (value, self->priv->has_primary_action);
      break;

    case PROP_HAS_SECONDARY_ACTION:
      g_value_set_boolean (value, self->priv->has_secondary_action);
      break;

    case PROP_SECONDARY_ACTION_NAME:
      g_value_set_string (value, self->priv->secondary_action_name ? self->priv->secondary_action_name : "");
      break;

    case PROP_TEXT:
      g_value_set_string (value, gtd_notification_get_text (self));
      break;

    case PROP_TIMEOUT:
      g_value_set_double (value, gtd_notification_get_timeout (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_notification_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GtdNotification *self = GTD_NOTIFICATION (object);

  switch (prop_id)
    {
    case PROP_SECONDARY_ACTION_NAME:
      gtd_notification_set_secondary_action (self,
                                             g_value_get_string (value),
                                             self->priv->secondary_action,
                                             self->priv->secondary_action_data);
      break;

    case PROP_TEXT:
      gtd_notification_set_text (self, g_value_get_string (value));
      break;

    case PROP_TIMEOUT:
      gtd_notification_set_timeout (self, g_value_get_double (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_notification_class_init (GtdNotificationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_notification_finalize;
  object_class->get_property = gtd_notification_get_property;
  object_class->set_property = gtd_notification_set_property;

  /**
   * GtdNotification::has-primary-action:
   *
   * @TRUE if the notification has a primary action or @FALSE otherwise. The
   * primary action is triggered on notification timeout or dismiss.
   */
  g_object_class_install_property (
        object_class,
        PROP_HAS_PRIMARY_ACTION,
        g_param_spec_boolean ("has-primary-action",
                              "Whether the notification has a primary action",
                              "Whether the notification has the primary action, activated on timeout or dismiss",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY));

  /**
   * GtdNotification::has-secondary-action:
   *
   * @TRUE if the notification has a secondary action or @FALSE otherwise. The
   * secondary action is triggered only by user explicit input.
   */
  g_object_class_install_property (
        object_class,
        PROP_HAS_SECONDARY_ACTION,
        g_param_spec_boolean ("has-secondary-action",
                              "Whether the notification has a secondary action",
                              "Whether the notification has the secondary action, activated by the user",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY));

  /**
   * GtdNotification::secondary-action-name:
   *
   * The main text of the notification, usually a markuped text.
   */
  g_object_class_install_property (
        object_class,
        PROP_SECONDARY_ACTION_NAME,
        g_param_spec_string ("secondary-action-name",
                             "Text of the secondary action button",
                             "The text of the secondary action button",
                             "",
                             G_PARAM_READWRITE));

  /**
   * GtdNotification::text:
   *
   * The main text of the notification, usually a markuped text.
   */
  g_object_class_install_property (
        object_class,
        PROP_TEXT,
        g_param_spec_string ("text",
                             "Notification message",
                             "The main message of the notification",
                             "",
                             G_PARAM_READWRITE));

  /**
   * GtdNotification::timeout:
   *
   * The time the notification will be displayed.
   */
  g_object_class_install_property (
        object_class,
        PROP_TIMEOUT,
        g_param_spec_double ("timeout",
                             "Notification timeout",
                             "The time the notification is displayed",
                             0.0,
                             30000.0,
                             7500.00,
                             G_PARAM_READWRITE));

  /**
   * GtdNotification::executed:
   *
   * The ::executed signal is emmited after the primary or secondary
   * #GtdNotification action is executed.
   */
  signals[EXECUTED] = g_signal_new ("executed",
                                    GTD_TYPE_NOTIFICATION,
                                    G_SIGNAL_RUN_FIRST,
                                    0,
                                    NULL,
                                    NULL,
                                    NULL,
                                    G_TYPE_NONE,
                                    0);
}

static void
gtd_notification_init (GtdNotification *self)
{
  self->priv = gtd_notification_get_instance_private (self);
  self->priv->secondary_action_name = NULL;
  self->priv->text = NULL;
  self->priv->timeout = 7500.0;
}

/**
 * gtd_notification_new:
 * @text: (nullable): text of the notification
 * @timeout: time for the notification to stay visible
 *
 * Creates a new notification with @text and @timeout. If @timeout is
 * 0, the notification is indefinitely displayed.
 *
 * Returns: (transfer full): a new #GtdNotification
 */
GtdNotification*
gtd_notification_new (const gchar *text,
                      gdouble      timeout)
{
  return g_object_new (GTD_TYPE_NOTIFICATION,
                       "text", text,
                       "timeout", timeout,
                       NULL);
}

/**
 * gtd_notification_set_primary_action:
 * @notification: a #GtdNotification
 * @func: (closure user_data) (scope call) (nullable): the primary action function
 * @user_data: data passed to @func
 *
 * Sets the primary action of @notification, which is triggered
 * on dismiss or timeout.
 */
void
gtd_notification_set_primary_action (GtdNotification           *notification,
                                     GtdNotificationActionFunc  func,
                                     gpointer                   user_data)
{
  GtdNotificationPrivate *priv;
  gboolean has_action;

  g_return_if_fail (GTD_IS_NOTIFICATION (notification));

  priv = notification->priv;
  has_action = (func != NULL);

  if (has_action != priv->has_primary_action)
    {
      priv->has_primary_action = has_action;

      priv->primary_action = has_action ? func : NULL;
      priv->primary_action_data = has_action ? user_data : NULL;

      g_object_notify (G_OBJECT (notification), "has-primary-action");
    }
}

/**
 * gtd_notification_set_secondary_action:
 * @notification: a #GtdNotification
 * @name: the name of the secondary action
 * @func: (closure user_data) (scope call) (nullable): the secondary action function
 * @user_data: data passed to @func
 *
 * Sets the secondary action of @notification, which is triggered
 * only on user explicit input.
 */
void
gtd_notification_set_secondary_action (GtdNotification           *notification,
                                       const gchar               *name,
                                       GtdNotificationActionFunc  func,
                                       gpointer                   user_data)
{
  GtdNotificationPrivate *priv;
  gboolean has_action;

  g_return_if_fail (GTD_IS_NOTIFICATION (notification));

  priv = notification->priv;
  has_action = (func != NULL);

  if (has_action != priv->has_secondary_action)
    {
      priv->has_secondary_action = has_action;

      priv->secondary_action = has_action ? func : NULL;
      priv->secondary_action_data = has_action ? user_data : NULL;

      if (priv->secondary_action_name != name)
        {
          g_clear_pointer (&priv->secondary_action_name, g_free);
          priv->secondary_action_name = g_strdup (name);

          g_object_notify (G_OBJECT (notification), "secondary-action-name");
        }

      g_object_notify (G_OBJECT (notification), "has-secondary-action");
    }
}

/**
 * gtd_notification_get_text:
 * @notification: a #GtdNotification
 *
 * Gets the text of @notification.
 *
 * Returns: (transfer none): the text of @notification.
 */
const gchar*
gtd_notification_get_text (GtdNotification *notification)
{
  g_return_val_if_fail (GTD_IS_NOTIFICATION (notification), NULL);

  return notification->priv->text ? notification->priv->text : "";
}

/**
 * gtd_notification_set_text:
 * @notification: a #GtdNotification
 * @text: the user-visible text of @notification
 *
 * Sets the text of @notification to @text.
 */
void
gtd_notification_set_text (GtdNotification *notification,
                           const gchar     *text)
{
  GtdNotificationPrivate *priv;

  g_return_if_fail (GTD_IS_NOTIFICATION (notification));

  priv = notification->priv;

  if (g_strcmp0 (priv->text, text) != 0)
    {
      g_clear_pointer (&priv->text, g_free);
      priv->text = g_strdup (text);

      g_object_notify (G_OBJECT (notification), "text");
    }
}

/**
 * gtd_notification_get_timeout:
 *
 * Retrieves the timeout of @notification.
 *
 * Returns: the timeout of @notification.
 */
gdouble
gtd_notification_get_timeout (GtdNotification *notification)
{
  g_return_val_if_fail (GTD_IS_NOTIFICATION (notification), 0.0);

  return notification->priv->timeout;
}

/**
 * gtd_notification_set_timeout:
 * @notification: a #GtdNotification
 * @timeout: the time to wait before running @notification, in miliseconds
 *
 * Sets the timeout of @notification to @timeout. Set it to %0 to disable
 * the timeout.
 */
void
gtd_notification_set_timeout (GtdNotification *notification,
                              gdouble          timeout)
{
  GtdNotificationPrivate *priv;

  g_return_if_fail (GTD_IS_NOTIFICATION (notification));

  priv = notification->priv;

  if (priv->timeout != timeout)
    {
      priv->timeout = timeout;

      g_object_notify (G_OBJECT (notification), "timeout");
    }
}

/**
 * gtd_notification_execute_primary_action:
 * @notification: a #GtdNotification
 *
 * Executes the primary action of @notification if set.
 */
void
gtd_notification_execute_primary_action (GtdNotification *notification)
{
  GtdNotificationPrivate *priv;

  g_return_if_fail (GTD_IS_NOTIFICATION (notification));

  priv = notification->priv;

  if (priv->primary_action)
    priv->primary_action (notification, priv->primary_action_data);

  g_signal_emit (notification, signals[EXECUTED], 0);
}

/**
 * gtd_notification_execute_secondary_action:
 * @notification: a #GtdNotification
 *
 * Executes the secondary action of @notification if any.
 */
void
gtd_notification_execute_secondary_action (GtdNotification *notification)
{
  GtdNotificationPrivate *priv;

  g_return_if_fail (GTD_IS_NOTIFICATION (notification));

  priv = notification->priv;

  if (priv->secondary_action)
    {
      priv->secondary_action (notification, priv->secondary_action_data);

      gtd_notification_stop (notification);

      g_signal_emit (notification, signals[EXECUTED], 0);
    }
}

/**
 * gtd_notification_start:
 * @notification: a #GtdNotification
 *
 * Starts the timeout of notification. Use gtd_notification_stop()
 * to stop it.
 */
void
gtd_notification_start (GtdNotification *notification)
{
  GtdNotificationPrivate *priv;

  g_return_if_fail (GTD_IS_NOTIFICATION (notification));

  priv = notification->priv;

  if (priv->timeout != 0)
    {
      if (priv->timeout_id > 0)
        {
          g_source_remove (priv->timeout_id);
          priv->timeout_id = 0;
        }

      priv->timeout_id = g_timeout_add (priv->timeout,
                                        (GSourceFunc) execute_action_cb,
                                        notification);
    }
}

/**
 * gtd_notification_stop:
 * @notification: a #GtdNotification
 *
 * Stops the timeout of notification. Use gtd_notification_start()
 * to start it.
 */
void
gtd_notification_stop (GtdNotification *notification)
{
  GtdNotificationPrivate *priv;

  g_return_if_fail (GTD_IS_NOTIFICATION (notification));

  priv = notification->priv;

  if (priv->timeout_id != 0)
    {
      g_source_remove (priv->timeout_id);
      priv->timeout_id = 0;
    }
}
