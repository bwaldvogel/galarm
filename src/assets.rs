use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::{env, fs, io, thread};

pub const ICON_PNG: &[u8] = include_bytes!("../pixmaps/galarm.png");
/// The same icon as raw ARGB32 pixels for the tray, generated with
/// `magick pixmaps/galarm.png -depth 8 RGBA:-` and moving the alpha byte to the front
pub const ICON_ARGB: &[u8] = include_bytes!("../pixmaps/galarm.argb");
pub const ICON_SIZE: i32 = 64;
const ALERT_SOUND: &[u8] = include_bytes!("../sounds/galarm-alert.ogg");

/// Writes the embedded data to the user's cache directory, since the
/// notification daemon and the sound player can only access files
pub fn cache_file(name: &str, data: &[u8]) -> io::Result<PathBuf> {
    let dir = env::var_os("XDG_CACHE_HOME")
        .filter(|dir| !dir.is_empty())
        .map(PathBuf::from)
        .or_else(|| env::var_os("HOME").map(|home| PathBuf::from(home).join(".cache")))
        .ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::NotFound,
                "neither XDG_CACHE_HOME nor HOME is set",
            )
        })?
        .join(crate::APP_NAME);
    let path = dir.join(name);
    if fs::read(&path).is_ok_and(|existing| existing == data) {
        return Ok(path);
    }
    fs::create_dir_all(&dir)?;
    fs::write(&path, data)?;
    Ok(path)
}

pub fn play_alert_sound() -> io::Result<()> {
    let path = cache_file("galarm-alert.ogg", ALERT_SOUND)?;
    for player in ["pw-play", "paplay"] {
        match Command::new(player)
            .arg(&path)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
        {
            Ok(mut child) => {
                thread::spawn(move || child.wait());
                return Ok(());
            }
            Err(e) if e.kind() == io::ErrorKind::NotFound => continue,
            Err(e) => return Err(e),
        }
    }
    Err(io::Error::new(
        io::ErrorKind::NotFound,
        "neither pw-play nor paplay found",
    ))
}
