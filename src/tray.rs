//! Tray icon via StatusNotifierItem

use std::sync::mpsc::Sender;

use ksni::blocking::{Handle, TrayMethods};
use ksni::menu::StandardItem;
use ksni::{Icon, MenuItem, ToolTip};

use crate::Event;
use crate::assets::{ICON_ARGB, ICON_SIZE};

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
        let mut items = Vec::new();
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
