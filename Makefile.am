# Copyright (C) 2014 Red Hat, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Author: Daiki Ueno

bin_PROGRAMS = xim-wayland

xim_wayland_SOURCES = xim.h xim.c main.c $(BUILT_SOURCES)
xim_wayland_CFLAGS = $(XCB_CFLAGS) $(WAYLAND_CFLAGS)
xim_wayland_LDADD = $(XCB_LIBS) $(WAYLAND_LIBS)

BUILT_SOURCES = text-client-protocol.h text-protocol.c
CLEANFILES = $(BUILT_SOURCES)

wayland_protocoldir = $(top_srcdir)
include $(top_srcdir)/wayland-scanner.mk

EXTRA_DIST = text.xml

-include $(top_srcdir)/git.mk
