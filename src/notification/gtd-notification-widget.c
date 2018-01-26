/* gtd-notification-widget.c
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

#define G_LOG_DOMAIN "GtdNotificationWidget"

#include "gtd-notification.h"
#include "gtd-notification-widget.h"

typedef enum
{
  STATE_IDLE,
  STATE_EXECUTING
} GtdExecutionState;

typedef struct
{
  /* widgets */
  GtkButton          *secondary_button;
  GtkSpinner         *spinner;
  GtkLabel           *text_label;

  /* internal data */
  GQueue             *queue;
  GtdNotification    *current_notification;
  GtdExecutionState   state;

  /* bindings */
  GBinding           *has_secondary_action_binding;
  GBinding           *message_label_binding;
  GBinding           *ready_binding;
  GBinding           *secondary_label_binding;
} GtdNotificationWidgetPrivate;

struct _GtdNotificationWidget
{
  GtkRevealer                   parent;

  /*< private >*/
  GtdNotificationWidgetPrivate *priv;
};

/* Prototypes */
static void         gtd_notification_widget_execute_notification (GtdNotificationWidget *widget,
                                                                  GtdNotification       *notification);

G_DEFINE_TYPE_WITH_PRIVATE (GtdNotificationWidget, gtd_notification_widget, GTK_TYPE_REVEALER)

static void
gtd_notification_widget_clear_bindings (GtdNotificationWidget *widget)
{
  GtdNotificationWidgetPrivate *priv = widget->priv;

  g_clear_pointer (&priv->has_secondary_action_binding, g_binding_unbind);
  g_clear_pointer (&priv->message_label_binding, g_binding_unbind);
  g_clear_pointer (&priv->ready_binding, g_binding_unbind);
  g_clear_pointer (&priv->secondary_label_binding, g_binding_unbind);
}

static void
gtd_notification_widget__enter_notify_cb (GtdNotificationWidget *widget)
{
  GtdNotificationWidgetPrivate *priv = widget->priv;

  /* Stop the timer when mouse enters */
  if (priv->current_notification)
    gtd_notification_stop (priv->current_notification);
}

static void
gtd_notification_widget__leave_notify_cb (GtdNotificationWidget *widget)
{
  GtdNotificationWidgetPrivate *priv = widget->priv;

  /* Restart the timer when mouse leaves */
  if (priv->current_notification)
    gtd_notification_start (priv->current_notification);
}

/*
 * This method is called after a notification is dismissed
 * or any action is taken, and it verifies if it should
 * continue the execution of notifications.
 */
static void
gtd_notification_widget_stop_or_run (GtdNotificationWidget *widget)
{
  GtdNotificationWidgetPrivate *priv = widget->priv;

  priv->current_notification = g_queue_pop_head (priv->queue);

  if (priv->current_notification)
    {
      gtk_revealer_set_reveal_child (GTK_REVEALER (widget), TRUE);
      gtd_notification_widget_execute_notification (widget, priv->current_notification);
      priv->state = STATE_EXECUTING;
    }
  else
    {
      gtk_revealer_set_reveal_child (GTK_REVEALER (widget), FALSE);
      priv->state = STATE_IDLE;
    }
}

static void
gtd_notification_widget__close_button_clicked_cb (GtdNotificationWidget *widget)
{
  GtdNotificationWidgetPrivate *priv = widget->priv;

  gtd_notification_stop (priv->current_notification);
  gtd_notification_execute_primary_action (priv->current_notification);
}

static void
gtd_notification_widget__secondary_button_clicked_cb (GtdNotificationWidget *widget)
{
  GtdNotificationWidgetPrivate *priv = widget->priv;

  gtd_notification_stop (priv->current_notification);
  gtd_notification_execute_secondary_action (priv->current_notification);
}

static void
gtd_notification_widget__notification_executed_cb (GtdNotification       *notification,
                                                   GtdNotificationWidget *widget)
{
  gtd_notification_widget_clear_bindings (widget);
  gtd_notification_widget_stop_or_run (widget);

  g_signal_handlers_disconnect_by_func (notification,
                                        gtd_notification_widget__notification_executed_cb,
                                        widget);
}

static void
gtd_notification_widget_execute_notification (GtdNotificationWidget *widget,
                                              GtdNotification       *notification)
{
  GtdNotificationWidgetPrivate *priv = widget->priv;

  g_signal_connect (notification,
                    "executed",
                    G_CALLBACK (gtd_notification_widget__notification_executed_cb),
                    widget);

  priv->has_secondary_action_binding =
          g_object_bind_property (notification,
                                  "has-secondary-action",
                                  priv->secondary_button,
                                  "visible",
                                  G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  priv->message_label_binding =
          g_object_bind_property (notification,
                                  "text",
                                  priv->text_label,
                                  "label",
                                  G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  priv->ready_binding =
          g_object_bind_property (notification,
                                  "ready",
                                  priv->spinner,
                                  "visible",
                                  G_BINDING_DEFAULT | G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);

  priv->secondary_label_binding =
          g_object_bind_property (notification,
                                  "secondary-action-name",
                                  priv->secondary_button,
                                  "label",
                                  G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  gtd_notification_start (notification);
}

static void
gtd_notification_widget_finalize (GObject *object)
{
  GtdNotificationWidget *self = (GtdNotificationWidget *)object;
  GtdNotificationWidgetPrivate *priv = gtd_notification_widget_get_instance_private (self);
  GList *l;

  /* When quitting, always execute the primary option of the notifications */
  if (priv->current_notification)
    {
      g_signal_handlers_disconnect_by_func (priv->current_notification,
                                            gtd_notification_widget__notification_executed_cb,
                                            self);

      gtd_notification_execute_primary_action (priv->current_notification);
    }

  for (l = priv->queue->head; l != NULL; l = l->next)
    gtd_notification_execute_primary_action (l->data);

  /* And only after executing all of them, release the queue */
  g_queue_free_full (priv->queue, g_object_unref);

  G_OBJECT_CLASS (gtd_notification_widget_parent_class)->finalize (object);
}

static void
gtd_notification_widget_class_init (GtdNotificationWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_notification_widget_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/notification.ui");

  gtk_widget_class_bind_template_child_private (widget_class, GtdNotificationWidget, secondary_button);
  gtk_widget_class_bind_template_child_private (widget_class, GtdNotificationWidget, spinner);
  gtk_widget_class_bind_template_child_private (widget_class, GtdNotificationWidget, text_label);

  gtk_widget_class_bind_template_callback (widget_class, gtd_notification_widget__close_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, gtd_notification_widget__enter_notify_cb);
  gtk_widget_class_bind_template_callback (widget_class, gtd_notification_widget__leave_notify_cb);
  gtk_widget_class_bind_template_callback (widget_class, gtd_notification_widget__secondary_button_clicked_cb);
}

static void
gtd_notification_widget_init (GtdNotificationWidget *self)
{
  self->priv = gtd_notification_widget_get_instance_private (self);
  self->priv->queue = g_queue_new ();
  self->priv->state = STATE_IDLE;

  gtk_widget_init_template (GTK_WIDGET (self));
}

/**
 * gtd_notification_widget_new:
 *
 * Creates a new #GtdNotificationWidget.
 *
 * Returns: (transfer full): a new #GtdNotificationWidget
 */
GtkWidget*
gtd_notification_widget_new (void)
{
  return g_object_new (GTD_TYPE_NOTIFICATION_WIDGET, NULL);
}

/**
 * gtd_notification_widget_notify:
 *
 * Adds @notification to the queue of notifications, and eventually
 * consume it.
 */
void
gtd_notification_widget_notify (GtdNotificationWidget *widget,
                                GtdNotification       *notification)
{
  GtdNotificationWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_NOTIFICATION_WIDGET (widget));

  priv = widget->priv;

  if (!g_queue_find (priv->queue, notification))
    {
      g_queue_push_tail (priv->queue, notification);

      if (priv->state == STATE_IDLE)
        gtd_notification_widget_stop_or_run (widget);
    }
}

/**
 * gtd_notification_widget_cancel:
 *
 * Cancel @notification from being displayed. If @notification is not
 * queued, nothing happens.
 */
void
gtd_notification_widget_cancel (GtdNotificationWidget *widget,
                                GtdNotification       *notification)
{
  GtdNotificationWidgetPrivate *priv;

  g_return_if_fail (GTD_IS_NOTIFICATION_WIDGET (widget));

  priv = widget->priv;

  if (notification == priv->current_notification)
    {
      gtd_notification_stop (notification);
      gtd_notification_widget_stop_or_run (widget);
    }
  else if (g_queue_find (priv->queue, notification))
    {
      g_queue_remove (priv->queue, notification);
    }
}
