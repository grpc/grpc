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
	"github.com/grpc/grpc/testctrl/svc/types"
)

type ComponentBuilder struct {
	container string
	kind      types.ComponentKind
	replicas  int32
	env       map[string]string
}

func NewComponentBuilder() *ComponentBuilder {
	return &ComponentBuilder{
		container: "example:latest",
		kind:      types.ClientComponent,
		replicas:  1,
		env:       make(map[string]string),
	}
}

func (cb *ComponentBuilder) Build() *types.Component {
	c := types.NewComponent(cb.container, cb.kind, cb.replicas)
	for k, v := range cb.env {
		c.SetEnv(k, v)
	}
	return c
}

func (cb *ComponentBuilder) SetContainer(c string) *ComponentBuilder {
	cb.container = c
	return cb
}

func (cb *ComponentBuilder) SetEnv(key, value string) *ComponentBuilder {
	cb.env[key] = value
	return cb
}

func (cb *ComponentBuilder) SetKind(k types.ComponentKind) *ComponentBuilder {
	cb.kind = k
	return cb
}

func (cb *ComponentBuilder) SetReplicas(r int32) *ComponentBuilder {
	cb.replicas = r
	return cb
}
