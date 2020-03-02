package types

import (
	pb "github.com/grpc/grpc/testctrl/genproto/testctrl/svc"
	"testing"
)

// TestComponentKindProto checks that the Proto method on the ComponentKind converts to the correct
// enum value in the protobuf generated code.
func TestComponentKindProto(t *testing.T) {
	expected := map[ComponentKind]pb.Component_Kind{
		DriverComponent: pb.Component_DRIVER,
		ClientComponent: pb.Component_CLIENT,
		ServerComponent: pb.Component_SERVER,
	}

	for k, v := range expected {
		if k.Proto() != v {
			t.Errorf("ComponentKind incorrectly converted to proto: expected %s but got %s", v.String(), k.String())
		}
	}
}

// TestComponentKindFromProto checks that the enum values in the protobuf generated code convert to
// the correct ComponentKind values.
func TestComponentKindFromProto(t *testing.T) {
	expected := map[pb.Component_Kind]ComponentKind{
		pb.Component_DRIVER: DriverComponent,
		pb.Component_CLIENT: ClientComponent,
		pb.Component_SERVER: ServerComponent,
	}

	for k, v := range expected {
		if ComponentKindFromProto(k) != v {
			t.Errorf("Proto incorrectly converted to ComponentKind: expected %s but got %s", v.String(), k.String())
		}
	}
}
