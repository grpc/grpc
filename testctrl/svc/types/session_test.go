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

// Package types_test is used to avoid a circular dependency, since these tests use the builders
// from the subpackage and the types package itself.
package types_test

import (
	"reflect"
	"testing"

	"github.com/grpc/grpc/testctrl/svc/types"
	"github.com/grpc/grpc/testctrl/svc/types/test"
)

func TestSessionFilterWorkers(t *testing.T) {
	driver := test.NewComponentBuilder().SetKind(types.DriverComponent).Build()
	client := test.NewComponentBuilder().SetKind(types.ClientComponent).Build()
	server := test.NewComponentBuilder().SetKind(types.ServerComponent).Build()
	session := test.NewSessionBuilder().SetComponents(driver, client, server).Build()

	cases := []struct {
		methodName string
		filterFunc func() []*types.Component
		expected   []*types.Component
	}{
		{"ClientWorkers", session.ClientWorkers, []*types.Component{client}},
		{"ServerWorkers", session.ServerWorkers, []*types.Component{server}},
	}

	for _, c := range cases {
		if results := c.filterFunc(); !reflect.DeepEqual(results, c.expected) {
			t.Errorf("Session %v included incompatible workers: expected %v but got %v", c.methodName, c.expected, results)
		}
	}
}
