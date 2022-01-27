// Copyright 2019 The gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package http2interop

import (
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"io"
	"net"
	"testing"
	"time"
)

const (
	Preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
)

var (
	defaultTimeout = 1 * time.Second
)

type HTTP2InteropCtx struct {
	// Inputs
	ServerHost             string
	ServerPort             int
	UseTLS                 bool
	UseTestCa              bool
	ServerHostnameOverride string

	T *testing.T

	// Derived
	serverSpec string
	authority  string
	rootCAs    *x509.CertPool
}

func parseFrame(r io.Reader) (Frame, error) {
	fh := FrameHeader{}
	if err := fh.Parse(r); err != nil {
		return nil, err
	}
	var f Frame
	switch fh.Type {
	case PingFrameType:
		f = &PingFrame{
			Header: fh,
		}
	case SettingsFrameType:
		f = &SettingsFrame{
			Header: fh,
		}
	case HTTP1FrameType:
		f = &HTTP1Frame{
			Header: fh,
		}
	default:
		f = &UnknownFrame{
			Header: fh,
		}
	}

	if err := f.ParsePayload(r); err != nil {
		return nil, err
	}

	return f, nil
}

func streamFrame(w io.Writer, f Frame) error {
	raw, err := f.MarshalBinary()
	if err != nil {
		return err
	}
	if _, err := w.Write(raw); err != nil {
		return err
	}
	return nil
}

func testClientShortSettings(ctx *HTTP2InteropCtx, length int) error {
	conn, err := connect(ctx)
	if err != nil {
		return err
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(defaultTimeout))

	if _, err := conn.Write([]byte(Preface)); err != nil {
		return err
	}

	// Bad, settings, non multiple of 6
	sf := &UnknownFrame{
		Header: FrameHeader{
			Type: SettingsFrameType,
		},
		Data: make([]byte, length),
	}
	if err := streamFrame(conn, sf); err != nil {
		ctx.T.Log("Unable to stream frame", sf)
		return err
	}

	if _, err := expectGoAwaySoon(conn); err != nil {
		return err
	}

	return nil
}

func testClientPrefaceWithStreamId(ctx *HTTP2InteropCtx) error {
	conn, err := connect(ctx)
	if err != nil {
		return err
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(defaultTimeout))

	// Good so far
	if _, err := conn.Write([]byte(Preface)); err != nil {
		return err
	}

	// Bad, settings do not have ids
	sf := &SettingsFrame{
		Header: FrameHeader{
			StreamID: 1,
		},
	}
	if err := streamFrame(conn, sf); err != nil {
		return err
	}

	if _, err := expectGoAwaySoon(conn); err != nil {
		return err
	}
	return nil
}

func testUnknownFrameType(ctx *HTTP2InteropCtx) error {
	conn, err := connect(ctx)
	if err != nil {
		return err
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(defaultTimeout))

	if err := http2Connect(conn, nil); err != nil {
		return err
	}

	// Write a bunch of invalid frame types.
	// Frame number 11 is the upcoming ALTSVC frame, and should not be tested.
	for ft := ContinuationFrameType + 2; ft != 0; ft++ {
		fh := &UnknownFrame{
			Header: FrameHeader{
				Type: ft,
			},
		}
		if err := streamFrame(conn, fh); err != nil {
			ctx.T.Log("Unable to stream frame", fh)
			return err
		}
	}

	pf := &PingFrame{
		Data: []byte("01234567"),
	}
	if err := streamFrame(conn, pf); err != nil {
		ctx.T.Log("Unable to stream frame", pf)
		return err
	}

	for {
		frame, err := parseFrame(conn)
		if err != nil {
			ctx.T.Log("Unable to parse frame", err)
			return err
		}
		if npf, ok := frame.(*PingFrame); !ok {
			ctx.T.Log("Got frame", frame.GetHeader().Type)
			continue
		} else {
			if string(npf.Data) != string(pf.Data) || npf.Header.Flags&PING_ACK == 0 {
				return fmt.Errorf("Bad ping %+v", *npf)
			}
			return nil
		}
	}

	return nil
}

func testShortPreface(ctx *HTTP2InteropCtx, prefacePrefix string) error {
	conn, err := connect(ctx)
	if err != nil {
		return err
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(defaultTimeout))

	if _, err := conn.Write([]byte(prefacePrefix)); err != nil {
		return err
	}

	if _, err := expectGoAwaySoon(conn); err != nil {
		return err
	}

	return nil
}

func testTLSMaxVersion(ctx *HTTP2InteropCtx, version uint16) error {
	config := buildTlsConfig(ctx)
	config.MaxVersion = version
	conn, err := connectWithTls(ctx, config)
	if err != nil {
		return err
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(defaultTimeout))

	if err := http2Connect(conn, nil); err != nil {
		return err
	}

	gf, err := expectGoAway(conn)
	if err != nil {
		return err
	}
	// TODO: make an enum out of this
	if gf.Code != 0xC {
		return fmt.Errorf("Expected an Inadequate security code: %v", gf)
	}
	return nil
}

func testTLSApplicationProtocol(ctx *HTTP2InteropCtx) error {
	config := buildTlsConfig(ctx)
	config.NextProtos = []string{"h2c"}
	conn, err := connectWithTls(ctx, config)
	if err != nil {
		return err
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(defaultTimeout))

	if err := http2Connect(conn, nil); err != nil {
		return err
	}

	gf, err := expectGoAway(conn)
	if err != nil {
		return err
	}
	// TODO: make an enum out of this
	if gf.Code != 0xC {
		return fmt.Errorf("Expected an Inadequate security code: %v", gf)
	}
	return nil
}

func testTLSBadCipherSuites(ctx *HTTP2InteropCtx) error {
	config := buildTlsConfig(ctx)
	// These are the suites that Go supports, but are forbidden by http2.
	config.CipherSuites = []uint16{
		tls.TLS_RSA_WITH_RC4_128_SHA,
		tls.TLS_RSA_WITH_3DES_EDE_CBC_SHA,
		tls.TLS_RSA_WITH_AES_128_CBC_SHA,
		tls.TLS_RSA_WITH_AES_256_CBC_SHA,
		tls.TLS_ECDHE_ECDSA_WITH_RC4_128_SHA,
		tls.TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
		tls.TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
		tls.TLS_ECDHE_RSA_WITH_RC4_128_SHA,
		tls.TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA,
		tls.TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
		tls.TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
	}
	conn, err := connectWithTls(ctx, config)
	if err != nil {
		return err
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(defaultTimeout))

	if err := http2Connect(conn, nil); err != nil {
		return err
	}

	gf, err := expectGoAway(conn)
	if err != nil {
		return err
	}
	// TODO: make an enum out of this
	if gf.Code != 0xC {
		return fmt.Errorf("Expected an Inadequate security code: %v", gf)
	}
	return nil
}

func expectGoAway(conn net.Conn) (*GoAwayFrame, error) {
	f, err := parseFrame(conn)
	if err != nil {
		return nil, err
	}
	if gf, ok := f.(*GoAwayFrame); !ok {
		return nil, fmt.Errorf("Expected GoAway Frame %+v", f)
	} else {
		return gf, nil
	}
}

// expectGoAwaySoon checks that a GOAWAY frame eventually comes.  Servers usually send
// the initial settings frames before any data has actually arrived.  This function
// checks that a go away shows.
func expectGoAwaySoon(conn net.Conn) (*GoAwayFrame, error) {
	for {
		f, err := parseFrame(conn)
		if err != nil {
			return nil, err
		}
		if gf, ok := f.(*GoAwayFrame); !ok {
			continue
		} else {
			return gf, nil
		}
	}
}

func http2Connect(c net.Conn, sf *SettingsFrame) error {
	if _, err := c.Write([]byte(Preface)); err != nil {
		return err
	}

	if sf == nil {
		sf = &SettingsFrame{}
	}
	if err := streamFrame(c, sf); err != nil {
		return err
	}
	return nil
}

// CapConn captures connection traffic if Log is non-nil
type CapConn struct {
	net.Conn
	Log func(args ...interface{})
}

func (c *CapConn) Write(data []byte) (int, error) {
	if c.Log != nil {
		c.Log(" SEND: ", data)
	}
	return c.Conn.Write(data)
}

func (c *CapConn) Read(data []byte) (int, error) {
	n, err := c.Conn.Read(data)
	if c.Log != nil {
		c.Log(" RECV: ", data[:n], err)
	}
	return n, err
}

func connect(ctx *HTTP2InteropCtx) (*CapConn, error) {
	var conn *CapConn
	var err error
	if !ctx.UseTLS {
		conn, err = connectWithoutTls(ctx)
	} else {
		config := buildTlsConfig(ctx)
		conn, err = connectWithTls(ctx, config)
	}
	if err != nil {
		return nil, err
	}
	conn.SetDeadline(time.Now().Add(defaultTimeout))

	return conn, nil
}

func buildTlsConfig(ctx *HTTP2InteropCtx) *tls.Config {
	return &tls.Config{
		RootCAs:    ctx.rootCAs,
		NextProtos: []string{"h2"},
		ServerName: ctx.authority,
		MinVersion: tls.VersionTLS12,
	}
}

func connectWithoutTls(ctx *HTTP2InteropCtx) (*CapConn, error) {
	conn, err := net.DialTimeout("tcp", ctx.serverSpec, defaultTimeout)
	if err != nil {
		return nil, err
	}
	return &CapConn{Conn: conn}, nil
}

func connectWithTls(ctx *HTTP2InteropCtx, config *tls.Config) (*CapConn, error) {
	conn, err := connectWithoutTls(ctx)
	if err != nil {
		return nil, err
	}

	return &CapConn{Conn: tls.Client(conn, config)}, nil
}
