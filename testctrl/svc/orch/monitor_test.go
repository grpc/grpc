package orch

import (
	"reflect"
	"testing"

	"github.com/grpc/grpc/testctrl/svc/types/test"
)

func TestMonitorAdd(t *testing.T) {
	obj := NewObjects(test.NewComponentBuilder().Build())[0]
	monitor := NewMonitor()

	monitor.Add(obj)

	actualObj := monitor.Get(obj.Name())
	if !reflect.DeepEqual(obj, actualObj) {
		t.Errorf("Monitor Add failed to add object")
	}
}

func TestMonitorRemove(t *testing.T) {
	obj := NewObjects(test.NewComponentBuilder().Build())[0]
	monitor := NewMonitor()

	monitor.Add(obj)
	monitor.Remove(obj.Name())

	remainingObj := monitor.Get(obj.Name())
	if remainingObj != nil {
		t.Errorf("Monitor Remove failed to remove object")
	}
}
