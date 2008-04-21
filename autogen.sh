#!/bin/sh
if [ ! -d config ]; then
	mkdir config
fi
if [ ! -f ChangeLog ]; then
	git-log > ChangeLog
fi
aclocal && autoheader && automake --gnu -a && autoconf
