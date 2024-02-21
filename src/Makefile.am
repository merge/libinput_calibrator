#  Copyright (C) 2024 Martin Kepplinger-Novakovic <martink@posteo.de>
#
# SPDX-License-Identifier: GPL-3.0

AM_CFLAGS               = $(DEBUGFLAGS)
AM_CPPFLAGS		= -I$(top_srcdir)/src

if LINUX
bin_PROGRAMS		= libinput_calibrator
endif

libinput_calibrator_SOURCES	= lc.c lc.h lc_common.c fbutils.h fbutils-linux.c font_8x8.c font_8x16.c font.h hypatia.h
