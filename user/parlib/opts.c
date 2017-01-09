/* Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Various option parsing utility functions. */

#include <parlib/opts.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* Parses an option file, calling parse() on individual lines.  We trim the
 * lines of any leading or trailing spaces first, and ignore anything after #.
 */
int parse_opts_file(char *opts_file, void (*parse)(char *))
{
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	FILE *fp;
	char *_line;

	if (!opts_file)
		return 0;
	fp = fopen(opts_file, "r");
	if (!fp)
		return -1;
	while ((read = getline(&line, &len, fp)) != -1) {
		_line = line;
		/* Drop comments */
		for (int i = 0; i < read; i++) {
			if (_line[i] == '#') {
				_line[i] = 0;
				read = i;
				break;
			}
		}
		if (!read)
			continue;
		/* Drop trailing spaces */
		for (int i = read - 1; i >= 0; i--) {
			if (isspace(_line[i]))
				_line[i] = 0;
			else
				break;
		}
		while (isspace(_line[read - 1])) {
			_line[read - 1] = 0;
			read--;
			if (!read)
				continue;
		}
		/* Drop leading spaces */
		while (isspace(_line[0]))
			_line++;
		parse(_line);
	}
	free(line);
	fclose(fp);
	return 0;
}
