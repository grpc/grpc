package http2interop

import (
	"io"
)

type Frame interface {
	GetHeader() *FrameHeader
	ParsePayload(io.Reader) error
	MarshalBinary() ([]byte, error)
}
