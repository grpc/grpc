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

	"k8s.io/api/core/v1"

	orchTest "github.com/grpc/grpc/testctrl/svc/orch/test"
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

func TestObjectUpdate(t *testing.T) {
	cases := []struct {
		phase          v1.PodPhase
		cstatus        orchTest.ContainerStateCase
		expectedHealth Health
	}{
		// pod pending cases
		{v1.PodPending, orchTest.ContainerStateCases.Terminated, Unhealthy},
		{v1.PodPending, orchTest.ContainerStateCases.Terminating, Unhealthy},
		{v1.PodPending, orchTest.ContainerStateCases.Waiting, Unknown},
		{v1.PodPending, orchTest.ContainerStateCases.CrashWaiting, Unhealthy},
		{v1.PodPending, orchTest.ContainerStateCases.Running, Unknown},
		{v1.PodPending, orchTest.ContainerStateCases.Empty, Unknown},

		// pod running cases
		{v1.PodRunning, orchTest.ContainerStateCases.Terminated, Unhealthy},
		{v1.PodRunning, orchTest.ContainerStateCases.Terminating, Unhealthy},
		{v1.PodRunning, orchTest.ContainerStateCases.Waiting, Unhealthy},
		{v1.PodRunning, orchTest.ContainerStateCases.CrashWaiting, Unhealthy},
		{v1.PodRunning, orchTest.ContainerStateCases.Running, Healthy},
		{v1.PodRunning, orchTest.ContainerStateCases.Empty, Unhealthy},

		// pod succeeded cases
		{v1.PodSucceeded, orchTest.ContainerStateCases.Terminated, Done},
		{v1.PodSucceeded, orchTest.ContainerStateCases.Terminating, Done},
		{v1.PodSucceeded, orchTest.ContainerStateCases.Waiting, Done},
		{v1.PodSucceeded, orchTest.ContainerStateCases.CrashWaiting, Failed},
		{v1.PodSucceeded, orchTest.ContainerStateCases.Running, Done},
		{v1.PodSucceeded, orchTest.ContainerStateCases.Empty, Done},

		// pod failed cases
		{v1.PodFailed, orchTest.ContainerStateCases.Terminated, Failed},
		{v1.PodFailed, orchTest.ContainerStateCases.Terminating, Failed},
		{v1.PodFailed, orchTest.ContainerStateCases.Waiting, Failed},
		{v1.PodFailed, orchTest.ContainerStateCases.CrashWaiting, Failed},
		{v1.PodFailed, orchTest.ContainerStateCases.Running, Failed},
		{v1.PodFailed, orchTest.ContainerStateCases.Empty, Failed},

		// pod unknown cases
		{v1.PodUnknown, orchTest.ContainerStateCases.Terminated, Unhealthy},
		{v1.PodUnknown, orchTest.ContainerStateCases.Terminating, Unhealthy},
		{v1.PodUnknown, orchTest.ContainerStateCases.Waiting, Unhealthy},
		{v1.PodUnknown, orchTest.ContainerStateCases.CrashWaiting, Unhealthy},
		{v1.PodUnknown, orchTest.ContainerStateCases.Running, Unhealthy},
		{v1.PodUnknown, orchTest.ContainerStateCases.Empty, Unknown},
	}

	for _, c := range cases {
		status := v1.PodStatus{
			Phase: c.phase,
			ContainerStatuses: []v1.ContainerStatus{
				c.cstatus.Status,
			},
		}

		o := NewObjects(test.NewComponentBuilder().Build())[0]
		o.Update(status)

		if o.Health() != c.expectedHealth {
			t.Errorf("Object Update set health '%v' but expected '%v' for pod phase '%v' and status '%v'",
				o.Health(), c.expectedHealth, c.phase, c.cstatus.Description)
		}
	}
}
