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
