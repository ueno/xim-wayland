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

AC_PREREQ(2.63)
AC_CONFIG_MACRO_DIR([m4])

AC_INIT([xim-wayland], [0.0.1], [ueno@gnu.org])

AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE([enable])
AC_GNU_SOURCE

AM_PROG_CC_C_O
AC_PROG_INSTALL

PKG_CHECK_MODULES([XCB], [xcb], ,
  [AC_MSG_ERROR([can't find xcb])])

PKG_CHECK_MODULES([WAYLAND], [wayland-client],
                  [enable_wayland_client=yes], [enable_wayland_client=no])
AC_PATH_PROG([wayland_scanner], [wayland-scanner])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
