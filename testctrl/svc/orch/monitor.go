package orch

import (
	"fmt"
	"sync"
)

// Monitor manages a list of objects, providing facilities to update them, check the aggregate
// health and consider errors across them. A monitor must be instantiated using the NewMonitor
// constructor.
type Monitor struct {
	objects   map[string]*Object
	errObject *Object
	mux       sync.Mutex
}

// NewMonitor creates a new monitor instance.
func NewMonitor() *Monitor {
	return &Monitor{
		objects: make(map[string]*Object),
	}
}

// Get finds and returns a managed object based on its component name. For more information, see
// the Name method on the type Object.
func (m *Monitor) Get(name string) *Object {
	m.mux.Lock()
	defer m.mux.Unlock()
	return m.objects[name]
}

// Add inserts an object into the set of objects the monitor manages.
func (m *Monitor) Add(o *Object) {
	m.mux.Lock()
	defer m.mux.Unlock()
	m.objects[o.Name()] = o
}

// Remove deletes an object from the set of objects the monitor manages.
func (m *Monitor) Remove(name string) {
	m.mux.Lock()
	defer m.mux.Unlock()
	delete(m.objects, name)
}

// Update accepts a kubernetes pod and proxies its new state to the appropriate managed object.
//
// An error is returned if the new state results in an unhealthy status on the pod, the pod passed
// as an argument does not reference any managed object, or the pod is missing the proper labels to
// resolve which object manages the pod.
func (m *Monitor) Update(pod *v1.Pod) error {
	componentName := pod.Labels["component-name"]
	if len(componentName) < 1 {
		return fmt.Errorf("monitor cannot update using pod named '%v', missing a component-name label", componentName)
	}

	m.mux.Lock()
	defer m.mux.Unlock()

	o := m.objects[componentName]
	if o == nil {
		return fmt.Errorf("monitor does not manage the component %v", componentName)
	}

	o.Update(pod.Status)
	if o.Health() == Unhealthy || o.Health() == Failed {
		return o.Error()
	}

	return nil
}

// Error is a convenience method that returns an error from the most recent unhealthy object.
func (m *Monitor) Error() error {
	m.mux.Lock()
	defer m.mux.Unlock()

	if m.errObject != nil {
		return m.errObject.Error()
	}

	return nil
}

// ErrObject returns the most recent unhealthy object.
func (m *Monitor) ErrObject() *Object {
	m.mux.Lock()
	defer m.mux.Unlock()
	return m.errObject
}

// Unhealthy returns true if any object has a health value of Unhealthy or Failed.
func (m *Monitor) Unhealthy() bool {
	m.mux.Lock()
	defer m.mux.Unlock()

	for _, o := range m.objects {
		if o.Health() == Unhealthy || o.Health() == Failed {
			m.errObject = o
			return true
		}
	}

	return false
}

// Done returns true if all objects have a health value of Done.
func (m *Monitor) Done() bool {
	m.mux.Lock()
	defer m.mux.Unlock()

	for _, o := range m.objects {
		if o.Health() != Done {
			return false
		}
	}

	return true
}
