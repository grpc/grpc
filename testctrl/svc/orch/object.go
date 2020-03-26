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

// ErrorContainerTerminated indicates that an object's docker container has terminated.
var ErrorContainerTerminated error

// ErrorContainerTerminating indicates that an object's docker container has begun terminating.
var ErrorContainerTerminating error

// ErrorContainerCrashed indicates that an object's docker container is in the waiting state, but
// a crash has been detected.
var ErrorContainerCrashed error

// ErrorPodFailed indicates that kubernetes marked an object's pod as failed.
var ErrorPodFailed error

// init, although normally discouraged, is only used to initialize the error variables, since they
// require the Errorf function from the fmt package.
func init() {
	ErrorContainerTerminated = fmt.Errorf("container terminated")
	ErrorContainerTerminating = fmt.Errorf("container terminating")
	ErrorContainerCrashed = fmt.Errorf("container crashed")
	ErrorPodFailed = fmt.Errorf("pod failed")
}

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
	return o.component.Name()
}

// Component is the component instance that the object wraps.
func (o *Object) Component() *types.Component {
	return o.component
}

// Update modifies a component's health and status by looking for errors in a kubernetes PodStatus.
func (o *Object) Update(status v1.PodStatus) {
	var err error
	phase := status.Phase
	if phase == v1.PodFailed {
		err = ErrorPodFailed
	}

	var cstate containerState
	if cstatuses := status.ContainerStatuses; len(cstatuses) > 0 {
		cstatus := &status.ContainerStatuses[0]
		if wcstate := cstatus.State.Waiting; wcstate != nil {
			if strings.Compare("CrashLoopBackOff", wcstate.Reason) == 0 {
				cstate = containerStateCrashWaiting
				err = ErrorContainerCrashed
			} else {
				cstate = containerStateWaiting
			}
		} else if cstatus.State.Terminated != nil {
			cstate = containerStateTerminated
			err = ErrorContainerTerminated
		} else if cstatus.LastTerminationState.Terminated != nil {
			cstate = containerStateTerminating
			err = ErrorContainerTerminating
		} else if cstatus.State.Running != nil {
			cstate = containerStateRunning
		} else {
			cstate = containerStateUnknown
		}
	}

	o.mux.Lock()
	defer o.mux.Unlock()

	psm, ok := phaseStateMap[phase]
	if !ok {
		o.health = Unknown
	}

	health, ok := psm[cstate]
	if ok {
		o.health = health
	} else {
		o.health = Unknown
	}

	o.podStatus = status
	if phase != v1.PodSucceeded {
		o.err = err
	}
}

// Health returns the health value that is currently affiliated with the object.
func (o *Object) Health() Health {
	o.mux.Lock()
	defer o.mux.Unlock()
	return o.health
}

// Unhealthy returns true if the object's health is marked as Unhealthy or Failed.
func (o *Object) Unhealthy() bool {
	o.mux.Lock()
	defer o.mux.Unlock()
	return o.health == Unhealthy || o.health == Failed
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

// containerState represents the current status of the single container in an object's pod. This
// does not have a 1:1 correlation with kubernetes, because it flattens their hierarchical
// structure.
type containerState int

const (
	// containerStateUnknown means that the docker container state objects did not conform to
	// any of the other containerState constants.
	containerStateUnknown containerState = iota

	// containerStateTerminated means the docker container has a non-nil terminating state
	// object specified as a previous state.
	containerStateTerminated

	// containerStateTerminating means the docker container has a non-nil terminating state
	// bject specified as the current state.
	containerStateTerminating

	// containerStateWaiting means the docker container is in the waiting state; however, a
	// crash has not been detected.
	containerStateWaiting

	// containerStateCrashWaiting means the docker container is marked in a waiting state, but
	// the reason is "CrashLoopBackOff". This means that there was a crash in the container,
	// and kubernetes will likely try to restart it.
	containerStateCrashWaiting

	// containerStateRunning means the docker container is marked in a running state.
	containerStateRunning
)

// phaseStateMap is a table that maps a kubernetes pod phase and the state of its container to a
// health value. For example, a PodRunning phase and a docker container state of
// containerStateCrashWaiting should map to Unhealthy.
var phaseStateMap = map[v1.PodPhase]map[containerState]Health{
	v1.PodPending: map[containerState]Health{
		containerStateUnknown:      Unknown,
		containerStateCrashWaiting: Unhealthy,
		containerStateTerminated:   Unhealthy,
		containerStateTerminating:  Unhealthy,
	},

	v1.PodRunning: map[containerState]Health{
		containerStateUnknown:      Unhealthy,
		containerStateRunning:      Healthy,
		containerStateWaiting:      Unhealthy,
		containerStateCrashWaiting: Unhealthy,
		containerStateTerminated:   Unhealthy,
		containerStateTerminating:  Unhealthy,
	},

	v1.PodSucceeded: map[containerState]Health{
		containerStateUnknown:      Done,
		containerStateRunning:      Done,
		containerStateWaiting:      Done,
		containerStateCrashWaiting: Failed,
		containerStateTerminated:   Done,
		containerStateTerminating:  Done,
	},

	v1.PodFailed: map[containerState]Health{
		containerStateUnknown:      Failed,
		containerStateRunning:      Failed,
		containerStateWaiting:      Failed,
		containerStateCrashWaiting: Failed,
		containerStateTerminated:   Failed,
		containerStateTerminating:  Failed,
	},

	v1.PodUnknown: map[containerState]Health{
		containerStateUnknown:      Unknown,
		containerStateRunning:      Unhealthy,
		containerStateWaiting:      Unhealthy,
		containerStateCrashWaiting: Unhealthy,
		containerStateTerminated:   Unhealthy,
		containerStateTerminating:  Unhealthy,
	},
}
