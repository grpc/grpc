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

package types

import (
	"fmt"

	"github.com/google/uuid"
	pb "github.com/grpc/grpc/testctrl/proto/scheduling/v1"
)

// Component represents a dependency that must be provisioned and managed during a session. For
// example, benchmarks tend to have a driver, server and a set of client components. Prefer creating
// components with the NewComponent constructor, because it assigns a unique name.
type Component struct {
	// Name uniquely identifies a component.
	Name string

	// ContainerImage is the docker image name and tag of the component.
	ContainerImage string

	// Kind is the type of component.
	Kind ComponentKind

	// PoolName is the optional name of a node pool where the component should be scheduled. If
	// no name is provided, the system will choose a pool.
	PoolName string

	// Env is the map of environment variables that should be set when the component is started.
	Env map[string]string
}

// NewComponent creates a Component instance, given a container image and kind.
//
// The container image string should contain the path to a docker image in a registry, versioned by
// an explicit tag or hash.
//
// For example:
//
//     // Container uses "nginx" image with explicit tag "latest" from docker hub
//     a := NewComponent("nginx:latest", ServerComponent)
//
//     // Container uses "java_worker" image with explicit tag "v3.2.2" from GCR
//     b := NewComponent("gcr.io/grpc-testing/java_worker:v3.2.2", ClientComponent)
//
//     // Container uses "driver" image with hash from GCR
//     c := NewComponent("gcr.io/grpc-testing/driver@sha256:e4ff8efd7eb62d3a3bb0990f6ff1e9df20da24ebf908d08f49cb83422a232862", DriverComponent)
func NewComponent(containerImage string, kind ComponentKind) *Component {
	return &Component{
		Name:           uuid.New().String(),
		ContainerImage: containerImage,
		Kind:           kind,
		Env:            make(map[string]string),
	}
}

// ResourceName returns the name the component prefixed with `components/`. This value should be
// displayed to a consumer of the API.
func (c *Component) ResourceName() string {
	return fmt.Sprintf("components/%s", c.Name)
}

// ComponentKind specifies the type of component the test requires.
type ComponentKind int32

const (
	_ ComponentKind = iota

	// DriverComponent is a test component that orchestrates workers, such as clients and servers.
	DriverComponent

	// ClientComponent feeds traffic to a server component.
	ClientComponent

	// ServerComponent accepts traffic from a client component.
	ServerComponent
)

// ComponentKindFromProto takes the generated protobuf enum type and safely converts it into a
// ComponentKind. It ensures the string representations are equivalent, even if values change.
func ComponentKindFromProto(p pb.Component_Kind) ComponentKind {
	return componentKindStringToConstMap[p.String()]
}

// String returns the string representation of the ComponentKind.
func (k ComponentKind) String() string {
	return componentKindConstToStringMap[k]
}

// Proto converts the ComponentKind enum into the generated protobuf enum. It ensures the string
// representations are equivalent, even if values change.
func (k ComponentKind) Proto() pb.Component_Kind {
	return pb.Component_Kind(pb.Component_Kind_value[k.String()])
}

var componentKindStringToConstMap = map[string]ComponentKind{
	"DRIVER": DriverComponent,
	"CLIENT": ClientComponent,
	"SERVER": ServerComponent,
}

var componentKindConstToStringMap = map[ComponentKind]string{
	DriverComponent: "DRIVER",
	ClientComponent: "CLIENT",
	ServerComponent: "SERVER",
}
