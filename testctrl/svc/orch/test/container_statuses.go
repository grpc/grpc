// INTERNAL TESTING CODE!

package test

import (
	"k8s.io/api/core/v1"
)

type ContainerStateCase struct {
	Description string
	Status      v1.ContainerStatus
}

var ContainerStateCases = struct {
	Terminating  ContainerStateCase
	Terminated   ContainerStateCase
	Waiting      ContainerStateCase
	CrashWaiting ContainerStateCase
	Running      ContainerStateCase
	Empty        ContainerStateCase
}{
	Terminating: ContainerStateCase{
		Description: "container terminating",
		Status: v1.ContainerStatus{
			State: v1.ContainerState{
				Terminated: &v1.ContainerStateTerminated{},
			},
		},
	},

	Terminated: ContainerStateCase{
		Description: "container terminated",
		Status: v1.ContainerStatus{
			LastTerminationState: v1.ContainerState{
				Terminated: &v1.ContainerStateTerminated{},
			},
		},
	},

	Waiting: ContainerStateCase{
		Description: "container waiting",
		Status: v1.ContainerStatus{
			State: v1.ContainerState{
				Waiting: &v1.ContainerStateWaiting{},
			},
		},
	},

	CrashWaiting: ContainerStateCase{
		Description: "container crash waiting",
		Status: v1.ContainerStatus{
			State: v1.ContainerState{
				Waiting: &v1.ContainerStateWaiting{
					Reason: "CrashLoopBackOff",
				},
			},
		},
	},

	Running: ContainerStateCase{
		Description: "container running",
		Status: v1.ContainerStatus{
			State: v1.ContainerState{
				Running: &v1.ContainerStateRunning{},
			},
		},
	},

	Empty: ContainerStateCase{
		Description: "no container state",
		Status: v1.ContainerStatus{
			State: v1.ContainerState{},
		},
	},
}
