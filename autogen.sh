#!/bin/sh
aclocal && autoheader && automake --gnu -a && autoconf
