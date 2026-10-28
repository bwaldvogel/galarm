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

    /// Shows the notification and returns its id together with a listener for
    /// its closing. The listener is subscribed before the notification is shown,
    /// so it can't miss the signal.
    pub fn show(
        &self,
        summary: &str,
        body: &str,
        icon_path: &str,
    ) -> zbus::Result<(u32, ClosedListener)> {
        let rule = MatchRule::builder()
            .msg_type(message::Type::Signal)
            .interface(INTERFACE)?
            .member("NotificationClosed")?
            .build();
        let signals = MessageIterator::for_match_rule(rule, &self.conn, None)?;

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
                Vec::<&str>::new(),
                hints,
                NEVER_EXPIRES,
            ),
        )?;
        let id: u32 = reply.body().deserialize()?;
        Ok((id, ClosedListener { id, signals }))
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

pub struct ClosedListener {
    id: u32,
    signals: MessageIterator,
}

impl ClosedListener {
    /// Blocks until the notification was closed, either by the user or because it expired
    pub fn wait(self) {
        for signal in self.signals.flatten() {
            if let Ok((id, _reason)) = signal.body().deserialize::<(u32, u32)>()
                && id == self.id
            {
                return;
            }
        }
    }
}
