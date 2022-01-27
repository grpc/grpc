// Copyright 2019 The gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package http2interop

import (
	"path"
	"runtime"
	"strings"
	"sync"
	"testing"
)

// When a test is skipped or fails, runtime.Goexit() is called which destroys the callstack.
// This means the name of the test case is lost, so we need to grab a copy of pc before.
func Report(t testing.TB) {
	// If the goroutine panics, Fatal()s, or Skip()s, the function name is at the 3rd callstack
	// layer.  On success, its at 1st.  Since it's hard to check which happened, just try both.
	pcs := make([]uintptr, 10)
	total := runtime.Callers(1, pcs)
	var name string
	for _, pc := range pcs[:total] {
		fn := runtime.FuncForPC(pc)
		fullName := fn.Name()
		if strings.HasPrefix(path.Ext(fullName), ".Test") {
			// Skip the leading .
			name = string([]byte(path.Ext(fullName))[1:])
			break
		}
	}
	if name == "" {
		return
	}

	allCaseInfos.lock.Lock()
	defer allCaseInfos.lock.Unlock()
	allCaseInfos.Cases = append(allCaseInfos.Cases, &caseInfo{
		Name:    name,
		Passed:  !t.Failed() && !t.Skipped(),
		Skipped: t.Skipped(),
		Fatal:   t.Failed() && !strings.HasPrefix(name, "TestSoon"),
	})
}

type caseInfo struct {
	Name    string `json:"name"`
	Passed  bool   `json:"passed"`
	Skipped bool   `json:"skipped,omitempty"`
	Fatal   bool   `json:"fatal,omitempty"`
}

type caseInfos struct {
	lock  sync.Mutex
	Cases []*caseInfo `json:"cases"`
}

var (
	allCaseInfos = caseInfos{}
)
