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
	"reflect"
	"testing"

	"github.com/grpc/grpc/testctrl/svc/types/test"
)

func TestMonitorAdd(t *testing.T) {
	obj := NewObjects(test.NewComponentBuilder().Build())[0]
	monitor := NewMonitor()

	monitor.Add(obj)

	actualObj := monitor.Get(obj.Name())
	if !reflect.DeepEqual(obj, actualObj) {
		t.Errorf("Monitor Add failed to add object")
	}
}

func TestMonitorRemove(t *testing.T) {
	obj := NewObjects(test.NewComponentBuilder().Build())[0]
	monitor := NewMonitor()

	monitor.Add(obj)
	monitor.Remove(obj.Name())

	remainingObj := monitor.Get(obj.Name())
	if remainingObj != nil {
		t.Errorf("Monitor Remove failed to remove object")
	}
}
