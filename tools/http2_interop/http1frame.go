package http2interop

import (
	"bytes"
	"io"
	"strings"
)

// HTTP1Frame is not a real frame, but rather a way to represent an http1.x response.
type HTTP1Frame struct {
	Header FrameHeader
	Data   []byte
}

func (f *HTTP1Frame) GetHeader() *FrameHeader {
	return &f.Header
}

func (f *HTTP1Frame) ParsePayload(r io.Reader) error {
	var buf bytes.Buffer
	if _, err := io.Copy(&buf, r); err != nil {
		return err
	}
	f.Data = buf.Bytes()
	return nil
}

func (f *HTTP1Frame) MarshalPayload() ([]byte, error) {
	return []byte(string(f.Data)), nil
}

func (f *HTTP1Frame) MarshalBinary() ([]byte, error) {
	buf, err := f.Header.MarshalBinary()
	if err != nil {
		return nil, err
	}

	buf = append(buf, f.Data...)

	return buf, nil
}

func (f *HTTP1Frame) String() string {
	s := string(f.Data)
	parts := strings.SplitN(s, "\n", 2)
	headerleft, _ := f.Header.MarshalBinary()

	return strings.TrimSpace(string(headerleft) + parts[0])
}
