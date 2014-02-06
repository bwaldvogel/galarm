 [![Build Status](https://travis-ci.org/bwaldvogel/galarm.png?branch=master)](https://travis-ci.org/bwaldvogel/galarm)

galarm
======

GTK tool to popup an alarm message and play a sound after a configurable time

Description
-----------

The main design goal was to create a *small* and *fast* starting CLI tool which
doesn't require any configuration. Instead you just start the tool with as **few
characters as possible**.

Usage
-----

popup after 5 minutes:

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

- GTK+ 2.12 or higher
- GLIB 2.14 or higher
- [libnotify][libnotify] 0.3.2 or higher to trigger the notification popup
- Optional: [libcanberra][libcanberra] 0.10 or higher for the event sound

…and a running **notification-daemon** to show popups

TODOs
-----

### Options:

	- stopall (stop ALL timers)
	- pauseall (pause ALL timers)
	- resumeall …

- recognize several running timers → collapse them
	- -c (dont collapse) or something like this

- add option to specify popup-urgency

- when activating the tray icon show a countdown window

- Internationalisation
	- 24h/12h format (configurable, dependent on locale)

- option to just count the time while beeing "online" so the time during
  suspend-to-ram eg. isn't counted

- [avoid gettimeofday()][gettimeofday]


[screenshot1]: http://galarm.0x11.net/screenshot1.png
[screenshot2]: http://galarm.0x11.net/screenshot2.png
[screenshot3]: http://galarm.0x11.net/screenshot3.png

[libnotify]: http://developer.gnome.org/libnotify/
[libcanberra]: http://0pointer.de/lennart/projects/libcanberra/

[gettimeofday]: http://blog.habets.pp.se/2010/09/gettimeofday-should-never-be-used-to-measure-time
