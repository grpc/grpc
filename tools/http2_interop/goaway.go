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
	"encoding/binary"
	"fmt"
	"io"
)

type GoAwayFrame struct {
	Header FrameHeader
	Reserved
	StreamID
	// TODO(carl-mastrangelo): make an enum out of this.
	Code uint32
	Data []byte
}

func (f *GoAwayFrame) GetHeader() *FrameHeader {
	return &f.Header
}

func (f *GoAwayFrame) ParsePayload(r io.Reader) error {
	raw := make([]byte, f.Header.Length)
	if _, err := io.ReadFull(r, raw); err != nil {
		return err
	}
	return f.UnmarshalPayload(raw)
}

func (f *GoAwayFrame) UnmarshalPayload(raw []byte) error {
	if f.Header.Length != len(raw) {
		return fmt.Errorf("Invalid Payload length %d != %d", f.Header.Length, len(raw))
	}
	if f.Header.Length < 8 {
		return fmt.Errorf("Invalid Payload length %d", f.Header.Length)
	}
	*f = GoAwayFrame{
		Reserved: Reserved(raw[0]>>7 == 1),
		StreamID: StreamID(binary.BigEndian.Uint32(raw[0:4]) & 0x7fffffff),
		Code:     binary.BigEndian.Uint32(raw[4:8]),
		Data:     []byte(string(raw[8:])),
	}

	return nil
}

func (f *GoAwayFrame) MarshalPayload() ([]byte, error) {
	raw := make([]byte, 8, 8+len(f.Data))
	binary.BigEndian.PutUint32(raw[:4], uint32(f.StreamID))
	binary.BigEndian.PutUint32(raw[4:8], f.Code)
	raw = append(raw, f.Data...)

	return raw, nil
}

func (f *GoAwayFrame) MarshalBinary() ([]byte, error) {
	payload, err := f.MarshalPayload()
	if err != nil {
		return nil, err
	}

	f.Header.Length = len(payload)
	f.Header.Type = GoAwayFrameType
	header, err := f.Header.MarshalBinary()
	if err != nil {
		return nil, err
	}

	header = append(header, payload...)

	return header, nil
}
