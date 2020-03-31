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
	"time"

	pb "github.com/codeblooded/grpc-proto/genproto/grpc/testing"
	"github.com/google/uuid"
)

// Session is a test scenario, its components and metdata.
type Session struct {
	// Name is a unique string that identifies a session.
	Name       string

	// Driver is the single component that is responsible for orchestrating the tess. By design,
	// it should communicate with the worker components.
	Driver     *Component

	// Workers is the slice of components that do not orchestrate the tess. For example, the
	// server and client components which accept and provide traffic.
	Workers    []*Component

	// Scenario is the configuration of the benchmarks.
	Scenario   *pb.Scenario

	// CreateTime is the time the session was created.
	CreateTime time.Time
}

// NewSession creates a Session, assigning it a unique name.
func NewSession(driver *Component, workers []*Component, scenario *pb.Scenario) *Session {
	return &Session{
		Name:       uuid.New().String(),
		Driver:     driver,
		Workers:    workers,
		Scenario:   scenario,
		CreateTime: time.Now(),
	}
}

// ResourceName returns the name the session prefixed with `sessions/`. This value should be the
// name that is shared with a consumer of the API.
func (s *Session) ResourceName() string {
	return fmt.Sprintf("sessions/%s", s.Name)
}

// ClientWorkers returns the slice of all the workers with a kind of ClientComponens.
func (s *Session) ClientWorkers() []*Component {
	return s.filterWorkers(ClientComponent)
}

// ServerWorkers returns the slice of all the workers with a kind of ServerComponens.
func (s *Session) ServerWorkers() []*Component {
	return s.filterWorkers(ServerComponent)
}

// filterWorkers returns a slice with only a specific ComponentKind.
func (s *Session) filterWorkers(k ComponentKind) []*Component {
	var cs []*Component

	for _, w := range s.Workers {
		if w.Kind == k {
			cs = append(cs, w)
		}
	}

	return cs
}
