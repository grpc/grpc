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
	"fmt"
	"strings"
	"sync"

	"k8s.io/api/core/v1"

	"github.com/grpc/grpc/testctrl/svc/types"
)

// Object is a Component coupled with its status, health, and kubernetes pod information. It is
// designed to be used internally. It should be instantiated with the NewObjects function. All
// methods are thread-safe.
type Object struct {
	component *types.Component
	podStatus v1.PodStatus
	health    Health
	mux       sync.Mutex
	err       error
}

// NewObjects is a constructor for an object that takes components as variadic arguments. For each
// component, it will return exactly one Object instance in the returned slice.
func NewObjects(cs ...*types.Component) []*Object {
	var objects []*Object

	for _, component := range cs {
		objects = append(objects, &Object{
			component: component,
		})
	}

	return objects
}

// Name provides convenient access to the component name.
func (o *Object) Name() string {
	return o.component.Name
}

// Component is the component instance that the object wraps.
func (o *Object) Component() *types.Component {
	return o.component
}

// Update modifies a component's health and status by looking for errors in a kubernetes PodStatus.
func (o *Object) Update(status v1.PodStatus) {
	o.mux.Lock()
	defer o.mux.Unlock()
	o.podStatus = status

	if count := len(status.ContainerStatuses); count != 1 {
		o.health = NotReady
		o.err = fmt.Errorf("pod has %v container statuses, expected 1", count)
		return
	}
	containerStatus := status.ContainerStatuses[0]

	terminationState := containerStatus.LastTerminationState.Terminated
	if terminationState == nil {
		terminationState = containerStatus.State.Terminated
	}

	if terminationState != nil {
		if terminationState.ExitCode == 0 {
			o.health = Succeeded
			o.err = nil
			return
		}

		o.health = Failed
		o.err = fmt.Errorf("container terminated unexpectedly: [%v] %v",
			terminationState.Reason, terminationState.Message)
		return
	}

	if waitingState := containerStatus.State.Waiting; waitingState != nil {
		if strings.Compare("CrashLoopBackOff", waitingState.Reason) == 0 {
			o.health = Failed
			o.err = fmt.Errorf("container crashed: [%v] %v",
				waitingState.Reason, waitingState.Message)
			return
		}
	}

	if containerStatus.State.Running != nil {
		o.health = Ready
		o.err = nil
		return
	}

	o.health = Unknown
	o.err = nil
}

// Health returns the health value that is currently affiliated with the object.
func (o *Object) Health() Health {
	o.mux.Lock()
	defer o.mux.Unlock()
	return o.health
}

// Error returns any error that caused the object to be unhealthy.
func (o *Object) Error() error {
	o.mux.Lock()
	defer o.mux.Unlock()
	return o.err
}

// PodStatus returns the kubernetes PodStatus object which was last supplied to the Update function.
func (o *Object) PodStatus() v1.PodStatus {
	o.mux.Lock()
	defer o.mux.Unlock()
	return o.podStatus
}
