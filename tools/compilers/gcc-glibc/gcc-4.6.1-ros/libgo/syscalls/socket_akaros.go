// socket_akaros.go -- Socket handling specific to Akaros.

// Copyright 2010 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package syscall

const SizeofSockaddrInet4 = 16
const SizeofSockaddrInet6 = 28
const SizeofSockaddrUnix = 110

type RawSockaddrInet4 struct {
	Len uint8;
	Family uint8;
	Port uint16;
	Addr [4]byte /* in_addr */;
	Zero [8]uint8;
}

func (sa *RawSockaddrInet4) setLen() Socklen_t {
	print("RawSockaddrInet4.setLen not yet implemented!")
	return 0
}

type RawSockaddrInet6 struct {
	Len uint8;
	Family uint8;
	Port uint16;
	Flowinfo uint32;
	Addr [16]byte /* in6_addr */;
	Scope_id uint32;
}

func (sa *RawSockaddrInet6) setLen() Socklen_t {
	print("RawSockaddrInet6.setLen not yet implemented!")
	return 0
}

type RawSockaddrUnix struct {
	Len uint8;
	Family uint8;
	Path [108]int8;
}

func (sa *RawSockaddrUnix) setLen(n int) {
	print("RawSockaddrUnix.setLen not yet implemented!")
}

func (sa *RawSockaddrUnix) getLen() (int, int) {
	print("RawSockaddrUnix.getLen not yet implemented!")
	return 0, ENOSYS
}

type RawSockaddr struct {
	Len uint8;
	Family uint8;
	Data [14]int8;
}

// BindToDevice binds the socket associated with fd to device.
func BindToDevice(fd int, device string) (errno int) {
	print("BindToDevice not yet implemented!")
	return ENOSYS
}

