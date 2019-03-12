/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#pragma once

#include <ros/event.h>

/* FD Tap commands.  The commands get passed to the device, but intermediate
 * code will process them to some extent. */
#define FDTAP_CMD_ADD 		1
#define FDTAP_CMD_REM 		2
#define FDTAP_CMD_MOD 		3

/* FD Tap Event/Filter types.  These are somewhat a mix of kqueue and epoll
 * filters and are in flux.  For instance, we don't support things like
 * ONESHOT/DISPATCH yet.
 *
 * When using these, you're communicating directly with the device, so really
 * anything goes, but we'll try to standardize on a few flags. */
#define FDTAP_FILT_READABLE	0x00000001
#define FDTAP_FILT_WRITABLE	0x00000002
#define FDTAP_FILT_WRITTEN	0x00000004
#define FDTAP_FILT_DELETED	0x00000008
#define FDTAP_FILT_ERROR	0x00000010	/* may overwrite *data */
#define FDTAP_FILT_RENAME	0x00000020
#define FDTAP_FILT_TRUNCATE	0x00000040
#define FDTAP_FILT_ATTRIB	0x00000080
#define FDTAP_FILT_PRIORITY	0x00000100
#define FDTAP_FILT_HANGUP	0x00000200
#define FDTAP_FILT_RDHUP	0x00000400

/* When an event on FD matches filter, that event will be sent to ev_q with
 * ev_id, with an optional data blob passed back.  The specifics will depend on
 * the type of ev_q used.  For a CEQ, the event will coalesce, and the data will
 * be a 'last write wins'. */
struct fd_tap_req {
	int				fd;
	int				cmd;
	int				filter;
	int				ev_id;
	struct event_queue		*ev_q;
	void				*data;
};
