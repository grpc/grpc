package main

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"os"
	"os/exec"
	"runtime"
	"sync"

	"strings"

	harness "github.com/envoyproxy/protoc-gen-validate/tests/harness/go"
	"github.com/golang/protobuf/proto"
	"golang.org/x/net/context"
)

func Harnesses(goFlag, ccFlag, javaFlag, pythonFlag bool, externalHarnessFlag string) []Harness {
	harnesses := make([]Harness, 0)
	if goFlag {
		harnesses = append(harnesses, InitHarness("tests/harness/go/main/go-harness"))
	}
	if ccFlag {
		harnesses = append(harnesses, InitHarness("tests/harness/cc/cc-harness"))
	}
	if javaFlag {
		harnesses = append(harnesses, InitHarness("tests/harness/java/java-harness"))
	}
	if pythonFlag {
		harnesses = append(harnesses, InitHarness("tests/harness/python/python-harness"))
	}
	if externalHarnessFlag != "" {
		harnesses = append(harnesses, InitHarness(externalHarnessFlag))
	}
	return harnesses
}

type Harness struct {
	Name string
	Exec func(context.Context, io.Reader) (*harness.TestResult, error)
}

func InitHarness(cmd string, args ...string) Harness {
	if runtime.GOOS == "windows" {
		// Bazel runfiles are not symlinked in on windows,
		// so we have to use the manifest instead. If the manifest
		// doesn't exist, assume we're running in a non-Bazel context
		f, err := os.Open("MANIFEST")
		if err == nil {
			defer f.Close()

			s := bufio.NewScanner(f)
			manifest := map[string]string{}
			for s.Scan() {
				values := strings.Split(s.Text(), " ")
				manifest[values[0]] = values[1]
			}
			for k, v := range manifest {
				if strings.Contains(k, cmd) {
					cmd = v
				}
			}
		}
	}

	return Harness{
		Name: cmd,
		Exec: initHarness(cmd, args...),
	}
}

func initHarness(cmd string, args ...string) func(context.Context, io.Reader) (*harness.TestResult, error) {
	return func(ctx context.Context, r io.Reader) (*harness.TestResult, error) {
		out, errs := getBuf(), getBuf()
		defer relBuf(out)
		defer relBuf(errs)

		cmd := exec.CommandContext(ctx, cmd, args...)
		cmd.Stdin = r
		cmd.Stdout = out
		cmd.Stderr = errs

		if err := cmd.Run(); err != nil {
			return nil, fmt.Errorf("[%s] failed execution (%v) - captured stderr:\n%s", cmdStr(cmd), err, errs.String())
		}
		if errs.Len() > 0 {
			fmt.Printf("captured stderr:\n%s", errs.String())
		}

		res := new(harness.TestResult)
		if err := proto.Unmarshal(out.Bytes(), res); err != nil {
			return nil, fmt.Errorf("[%s] failed to unmarshal result: %v", cmdStr(cmd), err)
		}

		return res, nil
	}
}

var bufPool = &sync.Pool{New: func() interface{} { return new(bytes.Buffer) }}

func getBuf() *bytes.Buffer {
	return bufPool.Get().(*bytes.Buffer)
}

func relBuf(b *bytes.Buffer) {
	b.Reset()
	bufPool.Put(b)
}

func cmdStr(cmd *exec.Cmd) string {
	return fmt.Sprintf("%s %s", cmd.Path, strings.Join(cmd.Args, " "))
}
