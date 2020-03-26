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

package types

import (
	"testing"

	pb "github.com/grpc/grpc/testctrl/proto/scheduling/v1"
)

// TestEventKindProto checks that the Proto method on the EventKind converts to the correct enum value
// in the protobuf generated code.
func TestEventKindProto(t *testing.T) {
	expected := map[EventKind]pb.Event_Kind{
		InternalErrorEvent: pb.Event_INTERNAL_ERROR,
		QueueEvent:         pb.Event_QUEUE,
		AcceptEvent:        pb.Event_ACCEPT,
		ProvisionEvent:     pb.Event_PROVISION,
		RunEvent:           pb.Event_RUN,
		DoneEvent:          pb.Event_DONE,
		ErrorEvent:         pb.Event_ERROR,
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
		pb.Event_INTERNAL_ERROR: InternalErrorEvent,
		pb.Event_QUEUE:          QueueEvent,
		pb.Event_ACCEPT:         AcceptEvent,
		pb.Event_PROVISION:      ProvisionEvent,
		pb.Event_RUN:            RunEvent,
		pb.Event_DONE:           DoneEvent,
		pb.Event_ERROR:          ErrorEvent,
	}

	for k, v := range expected {
		if EventKindFromProto(k) != v {
			t.Errorf("Proto incorrectly converted to EventKind: expected %s but got %s", v.String(), k.String())
		}
	}
}
