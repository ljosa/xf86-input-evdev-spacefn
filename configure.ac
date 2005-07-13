#  Copyright 2005 Adam Jackson.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Process this file with autoconf to produce a configure script

AC_PREREQ(2.57)
AC_INIT([xf86-input-evdev],
        0.1.0,
        [ajax@freedesktop.org],
        xf86-input-evdev)

AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_AUX_DIR(.)
AM_INIT_AUTOMAKE([dist-bzip2])

AM_MAINTAINER_MODE

DRIVER_NAME=evdev
AC_SUBST([DRIVER_NAME])

AM_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_LIBTOOL
AC_PROG_CC

AH_TOP([#include "xorg-server.h"])

#AC_DEFINE(XFree86LOADER,1,[Stub define for loadable drivers])
#
#AC_ARG_ENABLE(XINPUT, AS_HELP_STRING([--enable-xinput],
#              [Build XInput support (default: yes)]),
#              [XINPUT=$enableval],[XINPUT=yes])
#AM_CONDITIONAL(XINPUT, test "x$XINPUT" = "xyes")
#if test "x$XINPUT" = "xyes" ; then
#    AC_DEFINE(XINPUT,1,[Enable XInput support])
#fi
#
#AC_ARG_ENABLE(XKB, AS_HELP_STRING([--enable-xkb],
#              [Build XKB support (default: yes)]),
#              [XKB=$enableval],[XKB=yes])
#AM_CONDITIONAL(XKB, test "x$XKB" = "xyes")
#if test "x$XKB" = "xyes" ; then
#    AC_DEFINE(XKB,1,[Enable XKB support])
#fi

# Checks for pkg-config packages
PKG_CHECK_MODULES(XORG, xorg-server xproto)
inputdir=$(pkg-config --variable=moduledir xorg-server)/input
AC_SUBST([inputdir])
sdkdir=$(pkg-config --variable=sdkdir xorg-server)

CFLAGS="$XORG_CFLAGS "' -I$(top_srcdir)/src'
AC_SUBST([CFLAGS])
AC_SUBST([INCLUDES])

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC

AC_OUTPUT([Makefile src/Makefile man/Makefile])
