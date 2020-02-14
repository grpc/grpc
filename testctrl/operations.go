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

	emptyPb "github.com/golang/protobuf/ptypes/empty"
	pb "github.com/grpc/grpc/testctrl/proto"
)

type OperationsServerImpl struct {}

func (o *OperationsServerImpl) ListOperations(ctx context.Context, req *pb.ListOperationsRequest) (*pb.ListOperationsResponse, error) {
	return nil, nil
}

func (o *OperationsServerImpl) GetOperation(ctx context.Context, req *pb.GetOperationRequest) (*pb.Operation, error) {
	return nil, nil
}

func (o *OperationsServerImpl) DeleteOperation(ctx context.Context, req *pb.DeleteOperationRequest) (*emptyPb.Empty, error) {
	return nil, nil
}

func (o *OperationsServerImpl) CancelOperation(ctx context.Context, req *pb.CancelOperationRequest) (*emptyPb.Empty, error) {
	return nil, nil
}

func (o *OperationsServerImpl) WaitOperation(ctx context.Context, req *pb.WaitOperationRequest) (*pb.Operation, error) {
	return nil, nil
}

