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
