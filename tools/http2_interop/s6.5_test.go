package http2interop

import (
	"testing"
)

func TestSmallMaxFrameSize(t *testing.T) {
	if *testCase != "experimental" {
		t.SkipNow()
	}
	ctx := InteropCtx(t)
	err := testSmallMaxFrameSize(ctx)
	matchError(t, err, "Got goaway frame")
}
