[![Build](https://github.com/bwaldvogel/galarm/actions/workflows/build.yml/badge.svg)](https://github.com/bwaldvogel/galarm/actions/workflows/build.yml)

galarm
======

Tool to pop up an alarm message and play a sound after a configurable time

Description
-----------

The main design goal was to create a *small* and *fast-starting* CLI tool which
doesn't require any configuration. Instead, you just start the tool with as **few
characters as possible**.


Installation
------------

galarm requires [Rust](https://rustup.rs/) 1.89 or higher:

```
$ cargo install --git https://github.com/bwaldvogel/galarm
$ ln -s galarm ~/.cargo/bin/ga
```

Usage
-----

Pop up after 5 minutes:

	# ga 5

…with a message:

	# ga 5m tea is ready

…at a fixed time:

	# ga @12 dont forget the call

You can also pause/resume or cancel the alarms.

Screenshots
-----------

![screenshot 1][screenshot1]
![screenshot 2][screenshot2]
![screenshot 3][screenshot3]

Dependencies
------------

galarm has no library dependencies, but at runtime it needs

- a running **notification daemon** to show popups
- `pw-play` (PipeWire) or `paplay` (PulseAudio) to play the alarm sound
- a system tray that supports [StatusNotifierItem][sni] for the tray icon (optional)

TODOs
-----

### Options:

	- stopall (stop ALL timers)
	- pauseall (pause ALL timers)
	- resumeall …

- recognize several running timers → collapse them
	- -c (don't collapse) or something like this

- add option to specify popup-urgency

- when activating the tray icon show a countdown window

- Internationalisation
	- 24h/12h format (configurable, depending on the locale)

- option to only count time while the system is running, so that e.g. time
  spent in suspend-to-RAM isn't counted


[screenshot1]: http://galarm.0x11.net/screenshot1.png
[screenshot2]: http://galarm.0x11.net/screenshot2.png
[screenshot3]: http://galarm.0x11.net/screenshot3.png

[sni]: https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/
