xim-wayland
===========

xim-wayland is a protocol translator between XIM and the wl_text_input
protocol.  It runs as a standalone XIM server and intercepts XIM
requests.

Installation
=======

The code is mostly self-contained, including an XCB binding of XIM
server protocol (not using worn IMdkit), and doesn't require any extra
library except libwayland-client and libxcb.  To compile from git, do:

$ ./autogen.sh
$ make

Usage
=====

Under weston session with XWayland enabled, do:

$ ./xim-wayland --locale=en &
$ LANG=en_US.utf8 XMODIFIERS=@im=wayland xterm

Videos:
http://du-a.org/~ueno/junk/xim-wayland.webm (without client preedit support)
http://du-a.org/~ueno/junk/xim-wayland-gedit-xim.webm
