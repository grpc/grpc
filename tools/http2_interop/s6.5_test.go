package http2interop

import (
	"testing"
)

func TestSoonSmallMaxFrameSize(t *testing.T) {
	defer Report(t)
	if *testCase != "framing" {
		t.SkipNow()
	}
	ctx := InteropCtx(t)
	err := testSmallMaxFrameSize(ctx)
	matchError(t, err, "Got goaway frame")
}

func TestSoonAllSettingsFramesAcked(t *testing.T) {
	defer Report(t)
	if *testCase != "framing" {
		t.SkipNow()
	}
	ctx := InteropCtx(t)
	if err := testAllSettingsFramesAcked(ctx); err != nil {
		t.Fatal(err)
	}
}
