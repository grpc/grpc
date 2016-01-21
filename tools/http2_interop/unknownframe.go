package http2interop

import (
	"fmt"
	"io"
)

type UnknownFrame struct {
	Header FrameHeader
	Data   []byte
}

func (f *UnknownFrame) GetHeader() *FrameHeader {
	return &f.Header
}

func (f *UnknownFrame) ParsePayload(r io.Reader) error {
	raw := make([]byte, f.Header.Length)
	if _, err := io.ReadFull(r, raw); err != nil {
		return err
	}
	return f.UnmarshalPayload(raw)
}

func (f *UnknownFrame) UnmarshalPayload(raw []byte) error {
	if f.Header.Length != len(raw) {
		return fmt.Errorf("Invalid Payload length %d != %d", f.Header.Length, len(raw))
	}

	f.Data = []byte(string(raw))

	return nil
}

func (f *UnknownFrame) MarshalPayload() ([]byte, error) {
	return []byte(string(f.Data)), nil
}

func (f *UnknownFrame) MarshalBinary() ([]byte, error) {
	f.Header.Length = len(f.Data)
	buf, err := f.Header.MarshalBinary()
	if err != nil {
		return nil, err
	}

	payload, err := f.MarshalPayload()
	if err != nil {
		return nil, err
	}

	buf = append(buf, payload...)

	return buf, nil
}
