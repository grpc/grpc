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

const (
	SETTINGS_ACK = 1
)

type SettingsFrame struct {
	Header FrameHeader
	Params []SettingsParameter
}

type SettingsIdentifier uint16

const (
	SettingsHeaderTableSize      SettingsIdentifier = 1
	SettingsEnablePush           SettingsIdentifier = 2
	SettingsMaxConcurrentStreams SettingsIdentifier = 3
	SettingsInitialWindowSize    SettingsIdentifier = 4
	SettingsMaxFrameSize         SettingsIdentifier = 5
	SettingsMaxHeaderListSize    SettingsIdentifier = 6
)

const (
	SETTINGS_FLAG_ACK byte = 0x01
)

func (si SettingsIdentifier) String() string {
	switch si {
	case SettingsHeaderTableSize:
		return "SETTINGS_HEADER_TABLE_SIZE"
	case SettingsEnablePush:
		return "SETTINGS_ENABLE_PUSH"
	case SettingsMaxConcurrentStreams:
		return "SETTINGS_MAX_CONCURRENT_STREAMS"
	case SettingsInitialWindowSize:
		return "SETTINGS_INITIAL_WINDOW_SIZE"
	case SettingsMaxFrameSize:
		return "SETTINGS_MAX_FRAME_SIZE"
	case SettingsMaxHeaderListSize:
		return "SETTINGS_MAX_HEADER_LIST_SIZE"
	default:
		return fmt.Sprintf("SETTINGS_UNKNOWN(%d)", uint16(si))
	}
}

type SettingsParameter struct {
	Identifier SettingsIdentifier
	Value      uint32
}

func (f *SettingsFrame) GetHeader() *FrameHeader {
	return &f.Header
}

func (f *SettingsFrame) ParsePayload(r io.Reader) error {
	raw := make([]byte, f.Header.Length)
	if _, err := io.ReadFull(r, raw); err != nil {
		return err
	}
	return f.UnmarshalPayload(raw)
}

func (f *SettingsFrame) UnmarshalPayload(raw []byte) error {
	if f.Header.Length != len(raw) {
		return fmt.Errorf("Invalid Payload length %d != %d", f.Header.Length, len(raw))
	}

	if f.Header.Length%6 != 0 {
		return fmt.Errorf("Invalid Payload length %d", f.Header.Length)
	}

	f.Params = make([]SettingsParameter, 0, f.Header.Length/6)
	for i := 0; i < len(raw); i += 6 {
		f.Params = append(f.Params, SettingsParameter{
			Identifier: SettingsIdentifier(binary.BigEndian.Uint16(raw[i : i+2])),
			Value:      binary.BigEndian.Uint32(raw[i+2 : i+6]),
		})
	}
	return nil
}

func (f *SettingsFrame) MarshalPayload() ([]byte, error) {
	raw := make([]byte, len(f.Params)*6)
	for i, p := range f.Params {
		binary.BigEndian.PutUint16(raw[i*6:i*6+2], uint16(p.Identifier))
		binary.BigEndian.PutUint32(raw[i*6+2:i*6+6], p.Value)
	}
	return raw, nil
}

func (f *SettingsFrame) MarshalBinary() ([]byte, error) {
	payload, err := f.MarshalPayload()
	if err != nil {
		return nil, err
	}

	f.Header.Length = len(payload)
	f.Header.Type = SettingsFrameType
	header, err := f.Header.MarshalBinary()
	if err != nil {
		return nil, err
	}
	header = append(header, payload...)

	return header, nil
}
