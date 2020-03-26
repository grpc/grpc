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
	pb "github.com/codeblooded/grpc-proto/genproto/grpc/testing"
	"github.com/grpc/grpc/testctrl/svc/types"
)

type SessionBuilder struct {
	driver   *types.Component
	workers  []*types.Component
	scenario *pb.Scenario
}

func NewSessionBuilder() *SessionBuilder {
	return &SessionBuilder{
		driver:   NewComponentBuilder().SetKind(types.DriverComponent).Build(),
		workers:  []*types.Component{},
		scenario: nil,
	}
}

func (sb *SessionBuilder) AddWorkers(cs ...*types.Component) *SessionBuilder {
	for _, c := range cs {
		sb.workers = append(sb.workers, c)
	}
	return sb
}

func (sb *SessionBuilder) Build() *types.Session {
	return types.NewSession(sb.driver, sb.workers, sb.scenario)
}

func (sb *SessionBuilder) SetComponents(cs ...*types.Component) *SessionBuilder {
	for _, c := range cs {
		if c.Kind() == types.DriverComponent {
			sb.driver = c
		} else {
			sb.AddWorkers(c)
		}
	}
	return sb
}

func (sb *SessionBuilder) SetDriver(c *types.Component) *SessionBuilder {
	sb.driver = c
	return sb
}

func (sb *SessionBuilder) SetScenario(scen *pb.Scenario) *SessionBuilder {
	sb.scenario = scen
	return sb
}
