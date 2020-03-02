package types

import (
	pb "github.com/grpc/grpc/testctrl/genproto/testctrl/svc"
	"testing"
)

// TestEventKindProto checks that the Proto method on the EventKind converts to the correct enum value
// in the protobuf generated code.
func TestEventKindProto(t *testing.T) {
	expected := map[EventKind]pb.Event_Kind{
		InternalEvent:  pb.Event_INTERNAL,
		QueueEvent:     pb.Event_QUEUE,
		AcceptEvent:    pb.Event_ACCEPT,
		ProvisionEvent: pb.Event_PROVISION,
		RunEvent:       pb.Event_RUN,
		DoneEvent:      pb.Event_DONE,
		ErrorEvent:     pb.Event_ERROR,
	}

	for k, v := range expected {
		if k.Proto() != v {
			t.Errorf("EventKind incorrectly converted to proto: expected %s but got %s", v.String(), k.String())
		}
	}
}

// TestEventKindFromProto checks that the enum values in the protobuf generated code convert to the
// correct EventKind values.
func TestEventKindFromProto(t *testing.T) {
	expected := map[pb.Event_Kind]EventKind{
		pb.Event_INTERNAL:  InternalEvent,
		pb.Event_QUEUE:     QueueEvent,
		pb.Event_ACCEPT:    AcceptEvent,
		pb.Event_PROVISION: ProvisionEvent,
		pb.Event_RUN:       RunEvent,
		pb.Event_DONE:      DoneEvent,
		pb.Event_ERROR:     ErrorEvent,
	}

	for k, v := range expected {
		if EventKindFromProto(k) != v {
			t.Errorf("Proto incorrectly converted to EventKind: expected %s but got %s", v.String(), k.String())
		}
	}
}
