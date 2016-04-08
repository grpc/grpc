package http2interop

import (
	"encoding/binary"
	"fmt"
	"io"
)

type FrameHeader struct {
	Length   int
	Type     FrameType
	Flags    byte
	Reserved Reserved
	StreamID
}

type Reserved bool

func (r Reserved) String() string {
	if r {
		return "R"
	}
	return ""
}

func (fh *FrameHeader) Parse(r io.Reader) error {
	buf := make([]byte, 9)
	if _, err := io.ReadFull(r, buf); err != nil {
		return err
	}
	return fh.UnmarshalBinary(buf)
}

func (fh *FrameHeader) UnmarshalBinary(b []byte) error {
	if len(b) != 9 {
		return fmt.Errorf("Invalid frame header length %d", len(b))
	}
	*fh = FrameHeader{
		Length:   int(b[0])<<16 | int(b[1])<<8 | int(b[2]),
		Type:     FrameType(b[3]),
		Flags:    b[4],
		Reserved: Reserved(b[5]>>7 == 1),
		StreamID: StreamID(binary.BigEndian.Uint32(b[5:9]) & 0x7fffffff),
	}
	return nil
}

func (fh *FrameHeader) MarshalBinary() ([]byte, error) {
	buf := make([]byte, 9, 9+fh.Length)

	if fh.Length > 0xFFFFFF || fh.Length < 0 {
		return nil, fmt.Errorf("Invalid frame header length: %d", fh.Length)
	}
	if fh.StreamID < 0 {
		return nil, fmt.Errorf("Invalid Stream ID: %v", fh.StreamID)
	}

	buf[0], buf[1], buf[2] = byte(fh.Length>>16), byte(fh.Length>>8), byte(fh.Length)
	buf[3] = byte(fh.Type)
	buf[4] = fh.Flags
	var res uint32
	if fh.Reserved {
		res = 0x80000000
	}
	binary.BigEndian.PutUint32(buf[5:], uint32(fh.StreamID)|res)

	return buf, nil
}

type StreamID int32

type FrameType byte

func (ft FrameType) String() string {
	switch ft {
	case DataFrameType:
		return "DATA"
	case HeadersFrameType:
		return "HEADERS"
	case PriorityFrameType:
		return "PRIORITY"
	case ResetStreamFrameType:
		return "RST_STREAM"
	case SettingsFrameType:
		return "SETTINGS"
	case PushPromiseFrameType:
		return "PUSH_PROMISE"
	case PingFrameType:
		return "PING"
	case GoAwayFrameType:
		return "GOAWAY"
	case WindowUpdateFrameType:
		return "WINDOW_UPDATE"
	case ContinuationFrameType:
		return "CONTINUATION"
	case HTTP1FrameType:
		return "HTTP/1.? (Bad)"
	default:
		return fmt.Sprintf("UNKNOWN(%d)", byte(ft))
	}
}

// Types
const (
	DataFrameType         FrameType = 0
	HeadersFrameType      FrameType = 1
	PriorityFrameType     FrameType = 2
	ResetStreamFrameType  FrameType = 3
	SettingsFrameType     FrameType = 4
	PushPromiseFrameType  FrameType = 5
	PingFrameType         FrameType = 6
	GoAwayFrameType       FrameType = 7
	WindowUpdateFrameType FrameType = 8
	ContinuationFrameType FrameType = 9

	// HTTP1FrameType is not a real type, but rather a convenient way to check if the response
	// is an http response.  The type of a frame header is the 4th byte, which in an http1
	// response will be "HTTP/1.1 200 OK" or something like that.  The character for "P" is 80.
	HTTP1FrameType FrameType = 80
)
