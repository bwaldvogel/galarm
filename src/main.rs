mod alarm;
mod assets;
mod cli;
mod markup;
mod notify;
mod timeout;
mod tray;

use std::error::Error;
use std::io::IsTerminal;
use std::os::unix::process::CommandExt;
use std::path::Path;
use std::process::{Command, ExitCode, Stdio};
use std::sync::mpsc::{self, RecvTimeoutError, Sender};
use std::time::Duration;
use std::{env, io, thread};

use ksni::blocking::Handle;
use signal_hook::consts::{SIGINT, SIGTERM};
use signal_hook::iterator::Signals;

use crate::alarm::{Alarm, Now};
use crate::cli::{Cli, Options};
use crate::notify::Notifier;
use crate::timeout::Timeout;
use crate::tray::Tray;

pub const APP_NAME: &str = "galarm";

pub enum Event {
    TogglePause,
    Quit,
    NotificationClosed,
}

fn main() -> ExitCode {
    match try_main() {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("{e}");
            ExitCode::FAILURE
        }
    }
}

fn try_main() -> Result<(), Box<dyn Error>> {
    let options = match cli::parse(env::args().skip(1))? {
        Cli::Help => {
            println!("{}", cli::USAGE);
            return Ok(());
        }
        Cli::Version => {
            println!("{APP_NAME} version {}", env!("CARGO_PKG_VERSION"));
            return Ok(());
        }
        Cli::Run(options) => options,
    };

    let now = Now::current();
    let timeout = timeout::parse(&options.timeout, &now.zoned)?;
    if options.verbose {
        match &timeout {
            Timeout::Countdown(duration) => {
                println!("alarm in {:.0} seconds", duration.as_secs_f64())
            }
            Timeout::At(end) => println!("alarm at {}", end.strftime("%H:%M:%S")),
        }
    }

    // started from an interactive shell, give the terminal back right away
    if io::stdin().is_terminal() {
        return start_in_background();
    }

    run(Alarm::new(timeout, &now), &options)
}

/// Re-executes galarm detached from the terminal. Forking is no option, since
/// the process becomes multi-threaded. The own process group keeps Ctrl+C in
/// the terminal away from it. stdin is no terminal anymore, so the new process
/// doesn't re-execute itself again.
fn start_in_background() -> Result<(), Box<dyn Error>> {
    Command::new(env::current_exe()?)
        .args(env::args_os().skip(1))
        .current_dir("/")
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .process_group(0)
        .spawn()?;
    Ok(())
}

fn run(mut alarm: Alarm, options: &Options) -> Result<(), Box<dyn Error>> {
    let message = &options.message;
    let (events, received) = mpsc::channel();
    quit_on_signals(events.clone())?;

    let notifier =
        Notifier::connect().map_err(|e| format!("connecting to the session bus: {e}"))?;
    let icon_path = assets::cache_file("galarm.png", assets::ICON_PNG)?;

    let pausable = alarm.is_countdown().then_some(false);
    let tray = match tray::spawn(
        alarm.status(message, &Now::current()),
        pausable,
        events.clone(),
    ) {
        Ok(tray) => Some(tray),
        Err(e) => {
            if options.verbose {
                eprintln!("no tray icon: {e}");
            }
            None
        }
    };

    let mut notification = None;
    loop {
        let now = Now::current();
        let event = if notification.is_some() {
            received.recv().map_err(|_| RecvTimeoutError::Disconnected)
        } else if alarm.left(&now).is_zero() {
            update_tray(&tray, |tray| {
                tray.status = format!("alarm: {message}");
                tray.paused = None;
            });
            notification = Some(fire_alarm(
                &notifier,
                message,
                &icon_path,
                options,
                events.clone(),
            )?);
            continue;
        } else {
            let status = alarm.status(message, &now);
            update_tray(&tray, |tray| tray.status = status);
            received.recv_timeout(alarm.left(&now).min(Duration::from_secs(1)))
        };

        match event {
            Err(RecvTimeoutError::Timeout) => {}
            Ok(Event::TogglePause) if notification.is_none() => {
                alarm.toggle_pause(&Now::current());
                let paused = alarm.is_paused();
                update_tray(&tray, |tray| tray.paused = Some(paused));
            }
            Ok(Event::TogglePause) => {}
            Ok(Event::Quit | Event::NotificationClosed) | Err(RecvTimeoutError::Disconnected) => {
                break;
            }
        }
    }

    if let Some(id) = notification {
        let _ = notifier.close(id);
    }
    if let Some(tray) = tray {
        tray.shutdown().wait();
    }
    Ok(())
}

fn quit_on_signals(events: Sender<Event>) -> io::Result<()> {
    let mut signals = Signals::new([SIGINT, SIGTERM])?;
    thread::spawn(move || {
        if signals.forever().next().is_some() {
            let _ = events.send(Event::Quit);
        }
    });
    Ok(())
}

fn update_tray(tray: &Option<Handle<Tray>>, f: impl FnOnce(&mut Tray)) {
    if let Some(tray) = tray {
        tray.update(f);
    }
}

/// Shows the notification, plays the sound and returns the notification id
fn fire_alarm(
    notifier: &Notifier,
    message: &str,
    icon_path: &Path,
    options: &Options,
    events: Sender<Event>,
) -> Result<u32, Box<dyn Error>> {
    let (id, closed) = notifier
        .show(
            "Alarm",
            &markup::message(message),
            &icon_path.to_string_lossy(),
        )
        .map_err(|e| format!("showing the notification: {e}"))?;
    thread::spawn(move || {
        closed.wait();
        let _ = events.send(Event::NotificationClosed);
    });

    if !options.quiet
        && let Err(e) = assets::play_alert_sound()
        && options.verbose
    {
        eprintln!("playing the sound: {e}");
    }
    Ok(id)
}
