 [![Build Status](https://travis-ci.org/bwaldvogel/galarm.png?branch=master)](https://travis-ci.org/bwaldvogel/galarm)

galarm
======

GTK tool to popup an alarm message and play a sound after a configurable time

Screenshots
-----------

![screenshot 1][screenshot1]
![screenshot 2][screenshot2]
![screenshot 3][screenshot3]

Dependencies
------------

- [libnotify][1] for the popup message
- [libcanberra][2] to play the event sound


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

- avoid [gettimeofday()][3]

[screenshot1]: http://galarm.0x11.net/screenshot1.png
[screenshot2]: http://galarm.0x11.net/screenshot2.png
[screenshot3]: http://galarm.0x11.net/screenshot3.png

[1]: http://developer.gnome.org/libnotify/
[2]: http://0pointer.de/lennart/projects/libcanberra/
[3]: http://blog.habets.pp.se/2010/09/gettimeofday-should-never-be-used-to-measure-time
