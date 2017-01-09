/* Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Various option parsing utility functions. */

#pragma once

/* Parses an option file, calling parse() on individual lines.  We trim the
 * lines of any leading or trailing spaces first, and ignore anything after #.
 */
int parse_opts_file(char *opts_file, void (*parse)(char *));
