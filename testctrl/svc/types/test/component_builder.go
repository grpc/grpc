package test

import (
	"github.com/grpc/grpc/testctrl/svc/types"
)

type ComponentBuilder struct {
	container string
	kind types.ComponentKind
	replicas int32
	env map[string]string
}

func NewComponentBuilder() *ComponentBuilder {
	return &ComponentBuilder{
		container: "example:latest",
		kind: types.ClientComponent,
		replicas: 1,
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
