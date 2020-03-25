package orch

import (
	"testing"

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
