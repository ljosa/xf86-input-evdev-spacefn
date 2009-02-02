#  Copyright 2008 Red Hat, Inc.
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
#  THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
AM_CFLAGS = $(XORG_CFLAGS)

noinst_PROGRAMS=fakedev btn0 absrel abs dummy

fakedev_SOURCES=fakedev.c fakedev.h
fakedev_LDFLAGS=$(DLOPEN_LIBS) -rdynamic

LFLAGS=-shared -fPIC

btn0_SOURCES=btn0.c
btn0_LDFLAGS=$(LFLAGS)

absrel_SOURCES=absrel.c
absrel_LDFLAGS=$(LFLAGS)

abs_SOURCES=abs.c
abs_LDFLAGS=$(LFLAGS)

dummy_SOURCES=dummy.c
dummy_LDFLAGS=$(LFLAGS)
