// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Akaros-specific

package os

// Hostname returns the host name reported by the kernel.
func Hostname() (name string, err Error) {
	print("Hostname not yet implemented!")
	return string(""), NewError("Hostname")
}
