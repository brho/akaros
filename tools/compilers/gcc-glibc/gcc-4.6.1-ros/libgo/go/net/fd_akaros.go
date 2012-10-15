// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Waiting for FDs via epoll(7).

package net

import (
	"os"
)

type pollster struct {}

func newpollster() (p *pollster, err os.Error) {
	print("newpollster not yet implemented!")
	return nil, os.NewError("newpollster")
}

func (p *pollster) AddFD(fd int, mode int, repeat bool) os.Error {
	print("pollster.AddFD not yet implemented!")
	return os.NewError("pollseter.AddFD")
}

func (p *pollster) StopWaiting(fd int, bits uint) {
	print("pollster.StopWaiting not yet implemented!")
}

func (p *pollster) DelFD(fd int, mode int) {
	print("pollster.DelFD not yet implemented!")
}

func (p *pollster) WaitFD(nsec int64) (fd int, mode int, err os.Error) {
	print("pollster.DelFD not yet implemented!")
	return 0, 0, os.NewError("pollster.WaitFD")
}

func (p *pollster) Close() os.Error {
	print("pollster.Close not yet implemented!")
	return os.NewError("pollster.Close")
}
