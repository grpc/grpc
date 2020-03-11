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

package svc

import (
	"context"

	svcPb "github.com/grpc/grpc/testctrl/genproto/testctrl/svc"
	lrPb "google.golang.org/genproto/googleapis/longrunning"
)

type SchedulingServer struct {
}

func (s *SchedulingServer) StartTestSession(ctx context.Context, req *svcPb.StartTestSessionRequest) (*lrPb.Operation, error) {
	operation := new(lrPb.Operation)
	operation.Done = false
	return operation, nil
}
