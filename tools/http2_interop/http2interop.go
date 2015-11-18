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
	c, err := connect(ctx)
	if err != nil {
		return err
	}
	defer c.Close()

	if _, err := c.Write([]byte(Preface)); err != nil {
		return err
	}

	// Bad, settings, non multiple of 6
	sf := &UnknownFrame{
		Header: FrameHeader{
			Type: SettingsFrameType,
		},
		Data: make([]byte, length),
	}
	if err := streamFrame(c, sf); err != nil {
		ctx.T.Log("Unable to stream frame", sf)
		return err
	}

	for {
		if _, err := parseFrame(c); err != nil {
			ctx.T.Log("Unable to parse frame")
			return err
		}
	}

	return nil
}

func testClientPrefaceWithStreamId(ctx *HTTP2InteropCtx) error {
	c, err := connect(ctx)
	if err != nil {
		return err
	}
	defer c.Close()

	// Good so far
	if _, err := c.Write([]byte(Preface)); err != nil {
		return err
	}

	// Bad, settings do not have ids
	sf := &SettingsFrame{
		Header: FrameHeader{
			StreamID: 1,
		},
	}
	if err := streamFrame(c, sf); err != nil {
		return err
	}

	for {
		if _, err := parseFrame(c); err != nil {
			return err
		}
	}

	return nil
}

func testUnknownFrameType(ctx *HTTP2InteropCtx) error {
	c, err := connect(ctx)
	if err != nil {
		return err
	}
	defer c.Close()

	if _, err := c.Write([]byte(Preface)); err != nil {
		return err
	}

	// Send some settings, which are part of the client preface
	sf := &SettingsFrame{}
	if err := streamFrame(c, sf); err != nil {
		ctx.T.Log("Unable to stream frame", sf)
		return err
	}

	// Write a bunch of invalid frame types.
	for ft := ContinuationFrameType + 1; ft != 0; ft++ {
		fh := &UnknownFrame{
			Header: FrameHeader{
				Type: ft,
			},
		}
		if err := streamFrame(c, fh); err != nil {
			ctx.T.Log("Unable to stream frame", fh)
			return err
		}
	}

	pf := &PingFrame{
		Data: []byte("01234567"),
	}
	if err := streamFrame(c, pf); err != nil {
		ctx.T.Log("Unable to stream frame", sf)
		return err
	}

	for {
		frame, err := parseFrame(c)
		if err != nil {
			ctx.T.Log("Unable to parse frame")
			return err
		}
		if npf, ok := frame.(*PingFrame); !ok {
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
	c, err := connect(ctx)
	if err != nil {
		return err
	}
	defer c.Close()

	if _, err := c.Write([]byte(prefacePrefix)); err != nil {
		return err
	}

	buf := make([]byte, 256)
	for ; err == nil; _, err = c.Read(buf) {
	}
	// TODO: maybe check for a GOAWAY?
	return err
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

	buf := make([]byte, 256)
	if n, err := conn.Read(buf); err != nil {
		if n != 0 {
			return fmt.Errorf("Expected no bytes to be read, but was %d", n)
		}
		return err
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

	buf := make([]byte, 256)
	if n, err := conn.Read(buf); err != nil {
		if n != 0 {
			return fmt.Errorf("Expected no bytes to be read, but was %d", n)
		}
		return err
	}
	return nil
}

func connect(ctx *HTTP2InteropCtx) (net.Conn, error) {
	var conn net.Conn
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
		// TODO(carl-mastrangelo): remove this once all test certificates have been updated.
		InsecureSkipVerify: true,
	}
}

func connectWithoutTls(ctx *HTTP2InteropCtx) (net.Conn, error) {
	conn, err := net.DialTimeout("tcp", ctx.serverSpec, defaultTimeout)
	if err != nil {
		return nil, err
	}
	return conn, nil
}

func connectWithTls(ctx *HTTP2InteropCtx, config *tls.Config) (*tls.Conn, error) {
	conn, err := connectWithoutTls(ctx)
	if err != nil {
		return nil, err
	}

	return tls.Client(conn, config), nil
}
