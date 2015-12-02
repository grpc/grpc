package http2interop

import (
	"time"
)

// Section 6.5 says the minimum SETTINGS_MAX_FRAME_SIZE is 16,384
func testSmallMaxFrameSize(ctx *HTTP2InteropCtx) error {
	conn, err := connect(ctx)
	if err != nil {
		return err
	}
	defer conn.Close()
	conn.Log = ctx.T.Log
	conn.SetDeadline(time.Now().Add(defaultTimeout))

	sf := &SettingsFrame{
		Params: []SettingsParameter{{
			Identifier: SettingsMaxFrameSize,
			Value:      1<<14 - 1, // 1 less than the smallest maximum
		}},
	}

	if err := http2Connect(conn, sf); err != nil {
		return err
	}

	if _, err := expectGoAwaySoon(conn); err != nil {
		return err
	}

	return nil
}
