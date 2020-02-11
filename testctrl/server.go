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

package main

import (
	"context"

	"github.com/codeblooded/testctrl/proto"
	"github.com/google/uuid"
)

// TestCtrlServerImpl is an implementation of the TestCtrl service.  It
// handles scheduling tests and retrieving their results.  It is defined
// by proto/testctrl.proto.
type TestCtrlServerImpl struct {
	queue []*proto.TestInvocation
}

// RunTests schedules a set of tests for execution, assigning them a
// unique identifier, or `uid`, for further communication with the
// service.
func (s *TestCtrlServerImpl) RunTests(ctx context.Context, req *proto.RunTestsRequest) (*proto.TestInvocation, error) {
	invocation := &proto.TestInvocation{
		Uid:     uuid.New().String(),
		Status:  proto.TestStatus_QUEUED,
		Request: req,
	}
	s.queue = append(s.queue, invocation)
	return invocation, nil
}

// ListTests returns a list of all currently scheduled and completed tests.
// In the future, it should support filtering and pagination.
func (s *TestCtrlServerImpl) ListTests(ctx context.Context, req *proto.ListTestsRequest) (*proto.ListTestsResponse, error) {
	res := &proto.ListTestsResponse{
		Invocations: s.queue,
	}
	return res, nil
}

