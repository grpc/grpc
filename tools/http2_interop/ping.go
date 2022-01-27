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
	"fmt"
	"io"
)

type PingFrame struct {
	Header FrameHeader
	Data   []byte
}

const (
	PING_ACK = 0x01
)

func (f *PingFrame) GetHeader() *FrameHeader {
	return &f.Header
}

func (f *PingFrame) ParsePayload(r io.Reader) error {
	raw := make([]byte, f.Header.Length)
	if _, err := io.ReadFull(r, raw); err != nil {
		return err
	}
	return f.UnmarshalPayload(raw)
}

func (f *PingFrame) UnmarshalPayload(raw []byte) error {
	if f.Header.Length != len(raw) {
		return fmt.Errorf("Invalid Payload length %d != %d", f.Header.Length, len(raw))
	}
	if f.Header.Length != 8 {
		return fmt.Errorf("Invalid Payload length %d", f.Header.Length)
	}

	f.Data = []byte(string(raw))

	return nil
}

func (f *PingFrame) MarshalPayload() ([]byte, error) {
	if len(f.Data) != 8 {
		return nil, fmt.Errorf("Invalid Payload length %d", len(f.Data))
	}
	return []byte(string(f.Data)), nil
}

func (f *PingFrame) MarshalBinary() ([]byte, error) {
	payload, err := f.MarshalPayload()
	if err != nil {
		return nil, err
	}

	f.Header.Length = len(payload)
	f.Header.Type = PingFrameType
	header, err := f.Header.MarshalBinary()
	if err != nil {
		return nil, err
	}

	header = append(header, payload...)

	return header, nil
}
