/**
 * @file buffer_sync.h
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#pragma once

/* add the necessary profiling hooks */
int sync_start(void);

/* remove the hooks */
void sync_stop(void);

/* sync the given CPU's buffer */
void sync_buffer(int cpu);
