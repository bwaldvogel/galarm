//! Desktop notifications via D-Bus, see
//! https://specifications.freedesktop.org/notification-spec/latest/

use std::collections::HashMap;

use zbus::blocking::{Connection, MessageIterator};
use zbus::zvariant::Value;
use zbus::{MatchRule, message};

const DESTINATION: &str = "org.freedesktop.Notifications";
const PATH: &str = "/org/freedesktop/Notifications";
const INTERFACE: &str = "org.freedesktop.Notifications";
const NEVER_EXPIRES: i32 = 0;

pub struct Notifier {
    conn: Connection,
}

impl Notifier {
    pub fn connect() -> zbus::Result<Self> {
        Ok(Notifier {
            conn: Connection::session()?,
        })
    }

    /// Shows the notification with buttons for the given actions, as pairs of
    /// key and label. Returns its id together with a listener for its events.
    /// The listener is subscribed before the notification is shown, so it can't
    /// miss a signal.
    pub fn show(
        &self,
        summary: &str,
        body: &str,
        icon_path: &str,
        actions: &[(&str, &str)],
    ) -> zbus::Result<(u32, Listener)> {
        let rule = MatchRule::builder()
            .msg_type(message::Type::Signal)
            .interface(INTERFACE)?
            .build();
        let signals = MessageIterator::for_match_rule(rule, &self.conn, None)?;

        let actions: Vec<&str> = actions
            .iter()
            .flat_map(|&(key, label)| [key, label])
            .collect();
        let hints = HashMap::from([("urgency", Value::U8(1))]); // normal
        let reply = self.conn.call_method(
            Some(DESTINATION),
            PATH,
            Some(INTERFACE),
            "Notify",
            &(
                crate::APP_NAME,
                0u32,
                icon_path,
                summary,
                body,
                actions,
                hints,
                NEVER_EXPIRES,
            ),
        )?;
        let id: u32 = reply.body().deserialize()?;
        Ok((id, Listener { id, signals }))
    }

    pub fn close(&self, id: u32) -> zbus::Result<()> {
        self.conn.call_method(
            Some(DESTINATION),
            PATH,
            Some(INTERFACE),
            "CloseNotification",
            &id,
        )?;
        Ok(())
    }
}

pub enum NotificationEvent {
    ActionInvoked(String),
    /// By the user, because it expired or because of an action
    Closed,
}

/// Yields the events of one notification; blocks until the next one
pub struct Listener {
    id: u32,
    signals: MessageIterator,
}

impl Iterator for Listener {
    type Item = NotificationEvent;

    fn next(&mut self) -> Option<NotificationEvent> {
        for signal in self.signals.by_ref().flatten() {
            let body = signal.body();
            let member = signal.header().member().map(|member| member.to_string());
            match member.as_deref() {
                Some("ActionInvoked") => {
                    if let Ok((id, key)) = body.deserialize::<(u32, String)>()
                        && id == self.id
                    {
                        return Some(NotificationEvent::ActionInvoked(key));
                    }
                }
                Some("NotificationClosed") => {
                    if let Ok((id, _reason)) = body.deserialize::<(u32, u32)>()
                        && id == self.id
                    {
                        return Some(NotificationEvent::Closed);
                    }
                }
                _ => {}
            }
        }
        None
    }
}
