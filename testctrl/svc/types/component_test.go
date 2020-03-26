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
	pb "github.com/grpc/grpc/testctrl/proto/scheduling/v1"
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
