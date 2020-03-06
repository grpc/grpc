package test

import (
	"github.com/grpc/grpc/testctrl/svc/types"
	pb "github.com/grpc/grpc/testctrl/genproto/grpc/testing"
)

type SessionBuilder struct {
	driver *types.Component
	workers []*types.Component
	scenario *pb.Scenario
}

func NewSessionBuilder() *SessionBuilder {
	return &SessionBuilder{
		driver: NewComponentBuilder().SetKind(types.DriverComponent).Build(),
		workers: []*types.Component{},
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
