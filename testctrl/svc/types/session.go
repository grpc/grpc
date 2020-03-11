package types

import (
	"fmt"
	"time"

	"github.com/google/uuid"
	pb "github.com/grpc/grpc/testctrl/genproto/grpc/testing"
)

// Session is a test scenario, its components and metdata.
type Session struct {
	name       string
	driver     *Component
	workers    []*Component
	scenario   *pb.Scenario
	createTime time.Time
}

// NewSession creates a Session, assigning it a unique name.
func NewSession(driver *Component, workers []*Component, scenario *pb.Scenario) *Session {
	return &Session{
		name:       uuid.New().String(),
		driver:     driver,
		workers:    workers,
		scenario:   scenario,
		createTime: time.Now(),
	}
}

// Name returns the globally unique name that identifies this Session instance.
func (t *Session) Name() string {
	return t.name
}

// ResourceName returns the name the session prefixed with `sessions/`. This value should be the
// name that is shared with a consumer of the API.
func (t *Session) ResourceName() string {
	return fmt.Sprintf("sessions/%s", t.name)
}

// Driver returns the Session's driver component.
func (t *Session) Driver() *Component {
	return t.driver
}

// Workers returns the slice of the Session's worker components.
func (t *Session) Workers() []*Component {
	return t.workers
}

// ClientWorkers returns the slice of all the workers with a kind of ClientComponent.
func (t *Session) ClientWorkers() []*Component {
	return t.filterWorkers(ClientComponent)
}

// ServerWorkers returns the slice of all the workers with a kind of ServerComponent.
func (t *Session) ServerWorkers() []*Component {
	return t.filterWorkers(ServerComponent)
}

// Scenario returns the raw test scenario protobuf. The scenario is not parsed into
// implementation-specific types, because the service is designed to know nothing about the tests.
func (t *Session) Scenario() *pb.Scenario {
	return t.scenario
}

// CreateTime returns the time that the Session was instantiated.
func (t *Session) CreateTime() time.Time {
	return t.createTime
}

// filterWorkers returns a slice with only a specific ComponentKind.
func (t *Session) filterWorkers(k ComponentKind) []*Component {
	var cs []*Component

	for _, w := range t.workers {
		if w.Kind() == k {
			cs = append(cs, w)
		}
	}

	return cs
}
