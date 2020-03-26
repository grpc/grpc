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

	"k8s.io/api/core/v1"

	orchTest "github.com/grpc/grpc/testctrl/svc/orch/test"
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

func TestMonitorUpdate(t *testing.T) {
	obj := NewObjects(test.NewComponentBuilder().Build())[0]
	monitor := NewMonitor()
	monitor.Add(obj)

	// update the object with an unhealthy status
	pb := orchTest.NewPodBuilder()
	pb.SetComponent(obj.Component())
	pb.SetContainerStatus(orchTest.ContainerStateCases.CrashWaiting.Status)
	err := monitor.Update(pb.Build())
	if err == nil {
		t.Errorf("Monitor Update did not return error when pod became unhealthy")
	}

	// update the object with a healthy status
	pb = orchTest.NewPodBuilder()
	pb.SetComponent(obj.Component())
	pb.SetContainerStatus(orchTest.ContainerStateCases.Running.Status)
	pb.SetPhase(v1.PodRunning)

	err = monitor.Update(pb.Build())
	if err != nil {
		t.Errorf("Monitor Update returned an error for healthy pod")
	}
}

func TestMonitorUnhealthy(t *testing.T) {
	cases := []struct {
		states    []orchTest.ContainerStateCase
		unhealthy bool
	}{
		{
			states: []orchTest.ContainerStateCase{
				orchTest.ContainerStateCases.Running,
				orchTest.ContainerStateCases.Running,
				orchTest.ContainerStateCases.Terminating,
			},
			unhealthy: true,
		},
		{
			states: []orchTest.ContainerStateCase{
				orchTest.ContainerStateCases.Running,
				orchTest.ContainerStateCases.CrashWaiting,
			},
			unhealthy: true,
		},
		{
			states: []orchTest.ContainerStateCase{
				orchTest.ContainerStateCases.Running,
				orchTest.ContainerStateCases.Running,
			},
			unhealthy: false,
		},
	}

	for _, c := range cases {
		monitor := NewMonitor()
		for _, s := range c.states {
			o := NewObjects(test.NewComponentBuilder().Build())[0]
			monitor.Add(o)

			pb := orchTest.NewPodBuilder()
			pb.SetComponent(o.Component())
			pb.SetContainerStatus(s.Status)
			pod := pb.Build()

			monitor.Update(pod)
		}

		if health := monitor.Unhealthy(); health != c.unhealthy {
			t.Errorf("Monitor Unhealthy returned %v unexpectedly for states: %v", health, c.states)
		}
	}
}

func TestMonitorDone(t *testing.T) {
	cases := []struct {
		phases []v1.PodPhase
		done   bool
	}{
		{
			phases: []v1.PodPhase{
				v1.PodSucceeded,
				v1.PodRunning,
			},
			done: false,
		},
		{
			phases: []v1.PodPhase{
				v1.PodFailed,
				v1.PodSucceeded,
			},
			done: false,
		},
		{
			phases: []v1.PodPhase{
				v1.PodSucceeded,
				v1.PodSucceeded,
			},
			done: true,
		},
	}

	for _, c := range cases {
		monitor := NewMonitor()
		for _, phase := range c.phases {
			o := NewObjects(test.NewComponentBuilder().Build())[0]
			monitor.Add(o)

			pb := orchTest.NewPodBuilder()
			pb.SetComponent(o.Component())
			pb.SetPhase(phase)
			pod := pb.Build()

			monitor.Update(pod)
		}

		if done := monitor.Done(); done != c.done {
			t.Errorf("Monitor Done returned %v unexpectedly for states: %v", done, c.phases)
		}
	}
}
