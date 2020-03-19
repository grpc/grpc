package orch

import (
	"testing"

	"k8s.io/api/core/v1"

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
		cstatus        containerStateTestCase
		expectedHealth Health
	}{
		// pod pending cases
		{v1.PodPending, terminatedState, Unhealthy},
		{v1.PodPending, terminatingState, Unhealthy},
		{v1.PodPending, waitingState, Unknown},
		{v1.PodPending, crashWaitingState, Unhealthy},
		{v1.PodPending, runningState, Unknown},
		{v1.PodPending, emptyState, Unknown},

		// pod running cases
		{v1.PodRunning, terminatedState, Unhealthy},
		{v1.PodRunning, terminatingState, Unhealthy},
		{v1.PodRunning, waitingState, Unhealthy},
		{v1.PodRunning, crashWaitingState, Unhealthy},
		{v1.PodRunning, runningState, Healthy},
		{v1.PodRunning, emptyState, Unhealthy},

		// pod succeeded cases
		{v1.PodSucceeded, terminatedState, Done},
		{v1.PodSucceeded, terminatingState, Done},
		{v1.PodSucceeded, waitingState, Done},
		{v1.PodSucceeded, crashWaitingState, Failed},
		{v1.PodSucceeded, runningState, Done},
		{v1.PodSucceeded, emptyState, Done},

		// pod failed cases
		{v1.PodFailed, terminatedState, Failed},
		{v1.PodFailed, terminatingState, Failed},
		{v1.PodFailed, waitingState, Failed},
		{v1.PodFailed, crashWaitingState, Failed},
		{v1.PodFailed, runningState, Failed},
		{v1.PodFailed, emptyState, Failed},

		// pod unknown cases
		{v1.PodUnknown, terminatedState, Unhealthy},
		{v1.PodUnknown, terminatingState, Unhealthy},
		{v1.PodUnknown, waitingState, Unhealthy},
		{v1.PodUnknown, crashWaitingState, Unhealthy},
		{v1.PodUnknown, runningState, Unhealthy},
		{v1.PodUnknown, emptyState, Unknown},
	}

	for _, c := range cases {
		status := v1.PodStatus{
			Phase: c.phase,
			ContainerStatuses: []v1.ContainerStatus{
				c.cstatus.status,
			},
		}

		o := NewObjects(test.NewComponentBuilder().Build())[0]
		o.Update(status)

		if o.Health() != c.expectedHealth {
			t.Errorf("Object Update set health '%v' but expected '%v' for pod phase '%v' and status '%v'",
				o.Health(), c.expectedHealth, c.phase, c.cstatus.description)
		}
	}
}

type containerStateTestCase struct {
	description string
	status      v1.ContainerStatus
}

var terminatingState = containerStateTestCase{
	description: "container terminating",
	status: v1.ContainerStatus{
		State: v1.ContainerState{
			Terminated: &v1.ContainerStateTerminated{},
		},
	},
}

var terminatedState = containerStateTestCase{
	description: "container terminated",
	status: v1.ContainerStatus{
		LastTerminationState: v1.ContainerState{
			Terminated: &v1.ContainerStateTerminated{},
		},
	},
}

var waitingState = containerStateTestCase{
	description: "container waiting",
	status: v1.ContainerStatus{
		State: v1.ContainerState{
			Waiting: &v1.ContainerStateWaiting{},
		},
	},
}

var crashWaitingState = containerStateTestCase{
	description: "container crash waiting",
	status: v1.ContainerStatus{
		State: v1.ContainerState{
			Waiting: &v1.ContainerStateWaiting{
				Reason: "CrashLoopBackOff",
			},
		},
	},
}

var runningState = containerStateTestCase{
	description: "container running",
	status: v1.ContainerStatus{
		State: v1.ContainerState{
			Running: &v1.ContainerStateRunning{},
		},
	},
}

var emptyState = containerStateTestCase{
	description: "no container state",
	status: v1.ContainerStatus{
		State: v1.ContainerState{},
	},
}
