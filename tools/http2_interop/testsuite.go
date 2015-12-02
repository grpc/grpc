package http2interop

import (
	"runtime"
	"strings"
	"sync"
	"testing"
)

// When a test is skipped or fails, runtime.Goexit() is called which destroys the callstack.
// This means the name of the test case is lost, so we need to grab a copy of pc before.
func Report(t testing.TB) func() {
	pc, _, _, ok := runtime.Caller(1)
	if !ok {
		t.Fatal("Can't get caller info")
	}
	return func() {
		fn := runtime.FuncForPC(pc)
		fullName := fn.Name()
		name := strings.Split(fullName, ".")[1]
		allCaseInfos.lock.Lock()
		defer allCaseInfos.lock.Unlock()
		allCaseInfos.Cases = append(allCaseInfos.Cases, &caseInfo{
			Name:    name,
			Passed:  !t.Failed(),
			Skipped: t.Skipped(),
		})
	}
}

type caseInfo struct {
	Name    string `json:"name"`
	Passed  bool   `json:"passed"`
	Skipped bool   `json:"skipped"`
}

type caseInfos struct {
	lock  sync.Mutex
	Cases []*caseInfo `json:"cases"`
}

var (
	allCaseInfos = caseInfos{}
)
