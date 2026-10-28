//! Tray icon via StatusNotifierItem

use std::sync::mpsc::Sender;
use std::time::Duration;

use ksni::blocking::{Handle, TrayMethods};
use ksni::menu::{StandardItem, SubMenu};
use ksni::{Icon, MenuItem, ToolTip};

use crate::Event;
use crate::assets::{ICON_ARGB, ICON_SIZE};

/// Directly in the menu, so it's a single click
const EXTEND_TIME_QUICK: (&str, Duration) = ("+1 minute", Duration::from_secs(60));
/// In the "Extend time" submenu
const EXTEND_TIME_OPTIONS: [(&str, Duration); 3] = [
    ("+5 minutes", Duration::from_secs(5 * 60)),
    ("+15 minutes", Duration::from_secs(15 * 60)),
    ("+1 hour", Duration::from_secs(3600)),
];

pub struct Tray {
    pub status: String,
    /// None if the alarm can't be paused (anymore)
    pub paused: Option<bool>,
    events: Sender<Event>,
}

/// Fails if there is no system tray that supports StatusNotifierItem
pub fn spawn(
    status: String,
    paused: Option<bool>,
    events: Sender<Event>,
) -> Result<Handle<Tray>, ksni::Error> {
    Tray {
        status,
        paused,
        events,
    }
    .spawn()
}

impl ksni::Tray for Tray {
    // a left click opens the menu, too
    const MENU_ON_ACTIVATE: bool = true;

    fn id(&self) -> String {
        crate::APP_NAME.into()
    }

    fn title(&self) -> String {
        crate::APP_NAME.into()
    }

    fn icon_pixmap(&self) -> Vec<Icon> {
        vec![Icon {
            width: ICON_SIZE,
            height: ICON_SIZE,
            data: ICON_ARGB.to_vec(),
        }]
    }

    fn tool_tip(&self) -> ToolTip {
        ToolTip {
            title: self.status.clone(),
            ..Default::default()
        }
    }

    fn menu(&self) -> Vec<MenuItem<Self>> {
        let (quick_label, quick_duration) = EXTEND_TIME_QUICK;
        let mut items = vec![
            StandardItem {
                // an underscore marks an access key, two are a literal one
                label: self.status.replace('_', "__"),
                enabled: false,
                ..Default::default()
            }
            .into(),
            MenuItem::Separator,
            extend_time_item(quick_label, quick_duration),
            SubMenu {
                label: "Extend time".into(),
                submenu: EXTEND_TIME_OPTIONS
                    .into_iter()
                    .map(|(label, duration)| extend_time_item(label, duration))
                    .collect(),
                ..Default::default()
            }
            .into(),
        ];
        if let Some(paused) = self.paused {
            items.push(
                StandardItem {
                    label: if paused { "Continue" } else { "Pause" }.into(),
                    activate: Box::new(|tray: &mut Self| {
                        let _ = tray.events.send(Event::TogglePause);
                    }),
                    ..Default::default()
                }
                .into(),
            );
        }
        items.push(MenuItem::Separator);
        items.push(
            StandardItem {
                label: "Quit".into(),
                activate: Box::new(|tray: &mut Self| {
                    let _ = tray.events.send(Event::Quit);
                }),
                ..Default::default()
            }
            .into(),
        );
        items
    }
}

fn extend_time_item(label: &str, duration: Duration) -> MenuItem<Tray> {
    StandardItem {
        label: label.into(),
        activate: Box::new(move |tray: &mut Tray| {
            let _ = tray.events.send(Event::ExtendTime(duration));
        }),
        ..Default::default()
    }
    .into()
}
