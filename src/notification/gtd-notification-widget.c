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

struct _GtdNotificationWidget
{
  GtkRevealer         parent;

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
  GBinding           *loading_binding;
  GBinding           *secondary_label_binding;
};


static void          execute_notification                        (GtdNotificationWidget *self,
                                                                  GtdNotification       *notification);

static void          on_notification_executed_cb                 (GtdNotification       *notification,
                                                                  GtdNotificationWidget *self);

G_DEFINE_TYPE (GtdNotificationWidget, gtd_notification_widget, GTK_TYPE_REVEALER)

static void
clear_bindings (GtdNotificationWidget *self)
{
  g_clear_pointer (&self->has_secondary_action_binding, g_binding_unbind);
  g_clear_pointer (&self->message_label_binding, g_binding_unbind);
  g_clear_pointer (&self->loading_binding, g_binding_unbind);
  g_clear_pointer (&self->secondary_label_binding, g_binding_unbind);
}


/*
 * This method is called after a notification is dismissed
 * or any action is taken, and it verifies if it should
 * continue the execution of notifications.
 */
static void
stop_or_run_notifications (GtdNotificationWidget *self)
{
  self->current_notification = g_queue_pop_head (self->queue);

  if (self->current_notification)
    {
      gtk_revealer_set_reveal_child (GTK_REVEALER (self), TRUE);
      execute_notification (self, self->current_notification);
      self->state = STATE_EXECUTING;
    }
  else
    {
      gtk_revealer_set_reveal_child (GTK_REVEALER (self), FALSE);
      self->state = STATE_IDLE;
    }
}


static void
execute_notification (GtdNotificationWidget *self,
                      GtdNotification       *notification)
{
  g_signal_connect (notification,
                    "executed",
                    G_CALLBACK (on_notification_executed_cb),
                    self);

  self->has_secondary_action_binding = g_object_bind_property (notification,
                                                               "has-secondary-action",
                                                               self->secondary_button,
                                                               "visible",
                                                               G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  self->message_label_binding = g_object_bind_property (notification,
                                                        "text",
                                                        self->text_label,
                                                        "label",
                                                        G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  self->loading_binding = g_object_bind_property (notification,
                                                  "loading",
                                                  self->spinner,
                                                  "visible",
                                                  G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  self->secondary_label_binding = g_object_bind_property (notification,
                                                          "secondary-action-name",
                                                          self->secondary_button,
                                                          "label",
                                                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  gtd_notification_start (notification);
}


static void
on_motion_controller_enter_cb (GtkEventController    *controller,
                               GtdNotificationWidget *self)
{
  gtd_notification_stop (self->current_notification);
}

static void
on_motion_controller_leave_cb (GtkEventController    *controller,
                               GtdNotificationWidget *self)
{
  gtd_notification_start (self->current_notification);
}


static void
on_close_button_clicked_cb (GtdNotificationWidget *self)
{
  gtd_notification_stop (self->current_notification);
  gtd_notification_execute_primary_action (self->current_notification);
}

static void
on_secondary_button_clicked_cb (GtdNotificationWidget *self)
{
  gtd_notification_stop (self->current_notification);
  gtd_notification_execute_secondary_action (self->current_notification);
}

static void
on_notification_executed_cb (GtdNotification       *notification,
                             GtdNotificationWidget *self)
{
  clear_bindings (self);
  stop_or_run_notifications (self);

  g_signal_handlers_disconnect_by_func (notification,
                                        on_notification_executed_cb,
                                        self);
}

static void
gtd_notification_widget_finalize (GObject *object)
{
  GtdNotificationWidget *self = (GtdNotificationWidget *)object;
  GList *l;

  /* When quitting, always execute the primary option of the notifications */
  if (self->current_notification)
    {
      g_signal_handlers_disconnect_by_func (self->current_notification, on_notification_executed_cb, self);
      gtd_notification_execute_primary_action (self->current_notification);
    }

  for (l = self->queue->head; l != NULL; l = l->next)
    gtd_notification_execute_primary_action (l->data);

  /* And only after executing all of them, release the queue */
  if (self->queue)
    {
      g_queue_free_full (self->queue, g_object_unref);
      self->queue = NULL;
    }

  G_OBJECT_CLASS (gtd_notification_widget_parent_class)->finalize (object);
}

static void
gtd_notification_widget_class_init (GtdNotificationWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_notification_widget_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/notification.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdNotificationWidget, secondary_button);
  gtk_widget_class_bind_template_child (widget_class, GtdNotificationWidget, spinner);
  gtk_widget_class_bind_template_child (widget_class, GtdNotificationWidget, text_label);

  gtk_widget_class_bind_template_callback (widget_class, on_close_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_motion_controller_enter_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_motion_controller_leave_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_secondary_button_clicked_cb);
}

static void
gtd_notification_widget_init (GtdNotificationWidget *self)
{
  self->queue = g_queue_new ();
  self->state = STATE_IDLE;

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
gtd_notification_widget_notify (GtdNotificationWidget *self,
                                GtdNotification       *notification)
{
  g_return_if_fail (GTD_IS_NOTIFICATION_WIDGET (self));

  if (!g_queue_find (self->queue, notification))
    {
      g_queue_push_tail (self->queue, notification);

      if (self->state == STATE_IDLE)
        stop_or_run_notifications (self);
    }
}

/**
 * gtd_notification_widget_cancel:
 *
 * Cancel @notification from being displayed. If @notification is not
 * queued, nothing happens.
 */
void
gtd_notification_widget_cancel (GtdNotificationWidget *self,
                                GtdNotification       *notification)
{
  g_return_if_fail (GTD_IS_NOTIFICATION_WIDGET (self));

  if (notification == self->current_notification)
    {
      gtd_notification_stop (notification);
      stop_or_run_notifications (self);
    }
  else if (g_queue_find (self->queue, notification))
    {
      g_queue_remove (self->queue, notification);
    }
}
