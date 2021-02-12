package main

import (
	"bytes"
	"context"
	"fmt"
	"log"
	"sync"
	"time"

	harness "github.com/envoyproxy/protoc-gen-validate/tests/harness/go"
	"github.com/golang/protobuf/proto"
	"github.com/golang/protobuf/ptypes"
)

func Work(wg *sync.WaitGroup, in <-chan TestCase, out chan<- TestResult, harnesses []Harness) {
	for tc := range in {
		ok, skip := execTestCase(tc, harnesses)
		out <- TestResult{ok, skip}
	}
	wg.Done()
}

func execTestCase(tc TestCase, harnesses []Harness) (ok, skip bool) {
	any, err := ptypes.MarshalAny(tc.Message)
	if err != nil {
		log.Printf("unable to convert test case %q to Any - %v", tc.Name, err)
		return false, false
	}

	b, err := proto.Marshal(&harness.TestCase{Message: any})
	if err != nil {
		log.Printf("unable to marshal test case %q - %v", tc.Name, err)
		return false, false
	}

	ctx, cancel := context.WithTimeout(context.Background(), time.Second*5)
	defer cancel()

	wg := new(sync.WaitGroup)
	wg.Add(len(harnesses))

	errs := make(chan error, len(harnesses))
	skips := make(chan string, len(harnesses))

	for _, h := range harnesses {
		h := h
		go func() {
			defer wg.Done()

			res, err := h.Exec(ctx, bytes.NewReader(b))
			if err != nil {
				errs <- err
				return
			}

			if res.Error {
				errs <- fmt.Errorf("%s: internal harness error: %s", h.Name, res.Reason)
			} else if res.Valid != tc.Valid {
				if res.AllowFailure {
					skips <- fmt.Sprintf("%s: ignoring test failure: %s", h.Name, res.Reason)
				} else if tc.Valid {
					errs <- fmt.Errorf("%s: expected valid, got: %s", h.Name, res.Reason)
				} else {
					errs <- fmt.Errorf("%s: expected invalid, but got valid", h.Name)
				}
			}
		}()
	}

	wg.Wait()
	close(errs)
	close(skips)

	ok = true

	for err := range errs {
		log.Printf("[%s] %v", tc.Name, err)
		ok = false
	}
	for out := range skips {
		log.Printf("[%s] %v", tc.Name, out)
		skip = true
	}

	return
}
