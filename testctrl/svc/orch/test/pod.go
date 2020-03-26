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

	"github.com/grpc/grpc/testctrl/svc/types"
)

type PodBuilder struct {
	phase           v1.PodPhase
	component       *types.Component
	containerStatus v1.ContainerStatus
	labels          map[string]string
}

func NewPodBuilder() *PodBuilder {
	return &PodBuilder{
		phase:  v1.PodRunning,
		labels: make(map[string]string),
	}
}

func (pb *PodBuilder) SetContainerStatus(status v1.ContainerStatus) {
	pb.containerStatus = status
}

func (pb *PodBuilder) SetComponent(co *types.Component) {
	pb.component = co
	pb.labels["component-name"] = co.Name()
}

func (pb *PodBuilder) SetPhase(phase v1.PodPhase) {
	pb.phase = phase
}

func (pb *PodBuilder) Build() *v1.Pod {
	name := "unset"
	if pb.component != nil {
		name = pb.component.Name()
	}

	pod := new(v1.Pod)
	pod.ObjectMeta.Name = name
	pod.ObjectMeta.Labels = pb.labels
	pod.Status = v1.PodStatus{
		Phase: pb.phase,
		ContainerStatuses: []v1.ContainerStatus{
			pb.containerStatus,
		},
	}
	return pod
}
