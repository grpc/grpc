package http2interop

import (
	"crypto/tls"
	"crypto/x509"
	"strings"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"strconv"
	"testing"
)

var (
	serverHost = flag.String("server_host", "", "The host to test")
	serverPort = flag.Int("server_port", 443, "The port to test")
	useTls     = flag.Bool("use_tls", true, "Should TLS tests be run")
	// TODO: implement
	testCase              = flag.String("test_case", "", "What test cases to run")

	// The rest of these are unused, but present to fulfill the client interface
	serverHostOverride    = flag.String("server_host_override", "", "Unused")
	useTestCa             = flag.Bool("use_test_ca", false, "Unused")
	defaultServiceAccount = flag.String("default_service_account", "", "Unused")
	oauthScope            = flag.String("oauth_scope", "", "Unused")
	serviceAccountKeyFile = flag.String("service_account_key_file", "", "Unused")
)

func InteropCtx(t *testing.T) *HTTP2InteropCtx {
	ctx := &HTTP2InteropCtx{
		ServerHost:             *serverHost,
		ServerPort:             *serverPort,
		ServerHostnameOverride: *serverHostOverride,
		UseTLS:                 *useTls,
		UseTestCa:              *useTestCa,
		T:                      t,
	}

	ctx.serverSpec = ctx.ServerHost
	if ctx.ServerPort != -1 {
		ctx.serverSpec += ":" + strconv.Itoa(ctx.ServerPort)
	}
	if ctx.ServerHostnameOverride == "" {
		ctx.authority = ctx.ServerHost
	} else {
		ctx.authority = ctx.ServerHostnameOverride
	}

	if ctx.UseTestCa {
		// It would be odd if useTestCa was true, but not useTls.  meh
		certData, err := ioutil.ReadFile("src/core/tsi/test_creds/ca.pem")
		if err != nil {
			t.Fatal(err)
		}

		ctx.rootCAs = x509.NewCertPool()
		if !ctx.rootCAs.AppendCertsFromPEM(certData) {
			t.Fatal(fmt.Errorf("Unable to parse pem data"))
		}
	}

	return ctx
}

func (ctx *HTTP2InteropCtx) Close() error {
	// currently a noop
	return nil
}

func TestShortPreface(t *testing.T) {
	ctx := InteropCtx(t)
	for i := 0; i < len(Preface)-1; i++ {
		if err := testShortPreface(ctx, Preface[:i]+"X"); err != io.EOF {
			t.Error("Expected an EOF but was", err)
		}
	}
}

func TestUnknownFrameType(t *testing.T) {
	ctx := InteropCtx(t)
	if err := testUnknownFrameType(ctx); err != nil {
		t.Fatal(err)
	}
}

func TestTLSApplicationProtocol(t *testing.T) {
	ctx := InteropCtx(t)
	err := testTLSApplicationProtocol(ctx); 
	matchError(t, err, "EOF")
}

func TestTLSMaxVersion(t *testing.T) {
	ctx := InteropCtx(t)
	err := testTLSMaxVersion(ctx, tls.VersionTLS11);
	matchError(t, err, "EOF", "server selected unsupported protocol")
}

func TestClientPrefaceWithStreamId(t *testing.T) {
	ctx := InteropCtx(t)
	err := testClientPrefaceWithStreamId(ctx)
	matchError(t, err, "EOF")
}

func matchError(t *testing.T, err error, matches  ... string) {
  if err == nil {
    t.Fatal("Expected an error")
  }
  for _, s := range matches {
    if strings.Contains(err.Error(), s) {
      return
    }
  }
  t.Fatalf("Error %v not in %+v", err, matches)
}

func TestMain(m *testing.M) {
	flag.Parse()
	os.Exit(m.Run())
}
