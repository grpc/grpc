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

package orch

import (
	"testing"

	"github.com/grpc/grpc/testctrl/svc/types"
	"github.com/grpc/grpc/testctrl/svc/types/test"
)

func TestNewObjects(t *testing.T) {
	cs := []*types.Component{
		test.NewComponentBuilder().Build(),
		test.NewComponentBuilder().Build(),
	}
	objs := NewObjects(cs...)

	if len(objs) != len(cs) {
		t.Errorf("NewObjects did not create the correct number of objects, expected %v but got %v", len(cs), len(objs))
	}

	set := make(map[string]*types.Component)
	for _, o := range objs {
		set[o.Component().Name()] = o.Component()
	}

	for _, c := range cs {
		if x := set[c.Name()]; x == nil {
			t.Errorf("NewObjects did not create an object for component %v", c.Name())
		}
	}
}
