package http2interop

import (
	"crypto/tls"
	"fmt"
	"io"
	"log"
)

const (
	Preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
)

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

func getHttp2Conn(addr string) (*tls.Conn, error) {
	config := &tls.Config{
		InsecureSkipVerify: true,
		NextProtos:         []string{"h2"},
	}

	conn, err := tls.Dial("tcp", addr, config)
	if err != nil {
		return nil, err
	}

	return conn, nil
}

func testClientShortSettings(addr string, length int) error {
	c, err := getHttp2Conn(addr)
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
		return err
	}

	for {
		frame, err := parseFrame(c)
		if err != nil {
			return err
		}
		log.Println(frame)
	}

	return nil
}

func testClientPrefaceWithStreamId(addr string) error {
	c, err := getHttp2Conn(addr)
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
		frame, err := parseFrame(c)
		if err != nil {
			return err
		}
		log.Println(frame)
	}

	return nil
}

func testUnknownFrameType(addr string) error {
	c, err := getHttp2Conn(addr)
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
			return err
		}
	}

	pf := &PingFrame{
		Data: []byte("01234567"),
	}
	if err := streamFrame(c, pf); err != nil {
		return err
	}

	for {
		frame, err := parseFrame(c)
		if err != nil {
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

func testShortPreface(addr string, prefacePrefix string) error {
	c, err := getHttp2Conn(addr)
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

func testTLSMaxVersion(addr string, version uint16) error {
	config := &tls.Config{
		InsecureSkipVerify: true,
		NextProtos:         []string{"h2"},
		MaxVersion:         version,
	}
	conn, err := tls.Dial("tcp", addr, config)
	if err != nil {
		return err
	}
	defer conn.Close()

	buf := make([]byte, 256)
	if n, err := conn.Read(buf); err != nil {
		if n != 0 {
			return fmt.Errorf("Expected no bytes to be read, but was %d", n)
		}
		return err
	}
	return nil
}

func testTLSApplicationProtocol(addr string) error {
	config := &tls.Config{
		InsecureSkipVerify: true,
		NextProtos:         []string{"h2c"},
	}
	conn, err := tls.Dial("tcp", addr, config)
	if err != nil {
		return err
	}
	defer conn.Close()

	buf := make([]byte, 256)
	if n, err := conn.Read(buf); err != nil {
		if n != 0 {
			return fmt.Errorf("Expected no bytes to be read, but was %d", n)
		}
		return err
	}
	return nil
}
