package http2interop

import (
	"fmt"
	"time"
)

// Section 6.5 says the minimum SETTINGS_MAX_FRAME_SIZE is 16,384
func testSmallMaxFrameSize(ctx *HTTP2InteropCtx) error {
	conn, err := connect(ctx)
	if err != nil {
		return err
	}
	defer conn.Close()
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

// Section 6.5.3 says all settings frames must be acked.
func testAllSettingsFramesAcked(ctx *HTTP2InteropCtx) error {
	conn, err := connect(ctx)
	if err != nil {
		return err
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(defaultTimeout))

	sf := &SettingsFrame{}
	if err := http2Connect(conn, sf); err != nil {
		return err
	}

	// The spec says "The values in the SETTINGS frame MUST be processed in the order they
	// appear. [...] Once all values have been processed, the recipient MUST immediately
	// emit a SETTINGS frame with the ACK flag set."  From my understanding, processing all
	// of no values warrants an ack per frame.
	for i := 0; i < 10; i++ {
		if err := streamFrame(conn, sf); err != nil {
			return err
		}
	}

	var settingsFramesReceived = 0
	// The server by default sends a settings frame as part of the handshake, and another
	// after the receipt of the initial settings frame as part of our conneection preface.
	// This means we expected 1 + 1 + 10 = 12 settings frames in return, with all but the
	// first having the ack bit.
	for settingsFramesReceived < 12 {
		f, err := parseFrame(conn)
		if err != nil {
			return err
		}

		// Other frames come down the wire too, including window update.  Just ignore those.
		if f, ok := f.(*SettingsFrame); ok {
			settingsFramesReceived += 1
			if settingsFramesReceived == 1 {
				if f.Header.Flags&SETTINGS_FLAG_ACK > 0 {
					return fmt.Errorf("settings frame should not have used ack: %v")
				}
				continue
			}

			if f.Header.Flags&SETTINGS_FLAG_ACK == 0 {
				return fmt.Errorf("settings frame should have used ack: %v", f)
			}
			if len(f.Params) != 0 {
				return fmt.Errorf("settings ack cannot have params: %v", f)
			}
		}
	}

	return nil
}
