// Copyright 2020 gRPC authors.
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

package types

import (
	"reflect"
	"testing"
)

func TestSessionFilterWorkers(t *testing.T) {
	driver := &Component{Kind: DriverComponent}
	client := &Component{Kind: ClientComponent}
	server := &Component{Kind: ServerComponent}

	session := &Session{
		Driver: driver,
		Workers: []*Component{server, client},
	}

	cases := []struct {
		methodName string
		filterFunc func() []*Component
		expected   []*Component
	}{
		{"ClientWorkers", session.ClientWorkers, []*Component{client}},
		{"ServerWorkers", session.ServerWorkers, []*Component{server}},
	}

	for _, c := range cases {
		if results := c.filterFunc(); !reflect.DeepEqual(results, c.expected) {
			t.Errorf("Session %v included incompatible workers: expected %v but got %v", c.methodName, c.expected, results)
		}
	}
}
