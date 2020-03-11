// Package types_test is used to avoid a circular dependency, since these tests use the builders
// from the subpackage and the types package itself.
package types_test

import (
  "reflect"
  "testing"

  "github.com/grpc/grpc/testctrl/svc/types"
  "github.com/grpc/grpc/testctrl/svc/types/test"
)

func TestSessionFilterWorkers(t *testing.T) {
  driver := test.NewComponentBuilder().SetKind(types.DriverComponent).Build()
  client := test.NewComponentBuilder().SetKind(types.ClientComponent).Build()
  server := test.NewComponentBuilder().SetKind(types.ServerComponent).Build()
  session := test.NewSessionBuilder().SetComponents(driver, client, server).Build()

  cases := []struct{
  	methodName string
    filterFunc func() []*types.Component
    expected []*types.Component
  }{
    {"ClientWorkers", session.ClientWorkers, []*types.Component{client}},
    {"ServerWorkers", session.ServerWorkers, []*types.Component{server}},
  }

  for _, c := range cases {
    if results := c.filterFunc(); !reflect.DeepEqual(results, c.expected) {
      t.Errorf("Session %v included incompatible workers: expected %v but got %v", c.methodName, c.expected, results)
    }
  }
}
