package http2interop

import (
	"crypto/tls"
	"flag"
	"io"
	"os"
	"testing"
)

var (
	serverSpec = flag.String("spec", ":50051", "The server spec to test")
)

func TestShortPreface(t *testing.T) {
	for i := 0; i < len(Preface)-1; i++ {
		if err := testShortPreface(*serverSpec, Preface[:i]+"X"); err != io.EOF {
			t.Error("Expected an EOF but was", err)
		}
	}
}

func TestUnknownFrameType(t *testing.T) {
	if err := testUnknownFrameType(*serverSpec); err != nil {
		t.Fatal(err)
	}
}

func TestTLSApplicationProtocol(t *testing.T) {
	if err := testTLSApplicationProtocol(*serverSpec); err != io.EOF {
		t.Fatal("Expected an EOF but was", err)
	}
}

func TestTLSMaxVersion(t *testing.T) {
	if err := testTLSMaxVersion(*serverSpec, tls.VersionTLS11); err != io.EOF {
		t.Fatal("Expected an EOF but was", err)
	}
}

func TestClientPrefaceWithStreamId(t *testing.T) {
	if err := testClientPrefaceWithStreamId(*serverSpec); err != io.EOF {
		t.Fatal("Expected an EOF but was", err)
	}
}

func TestMain(m *testing.M) {
	flag.Parse()
	os.Exit(m.Run())
}
