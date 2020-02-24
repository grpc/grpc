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

	emptyPb "github.com/golang/protobuf/ptypes/empty"
	lrPb "github.com/grpc/grpc/testctrl/genproto/google.golang.org/genproto/googleapis/longrunning"
)

type OperationsServer struct{}

func (o *OperationsServer) ListOperations(ctx context.Context, req *lrPb.ListOperationsRequest) (*lrPb.ListOperationsResponse, error) {
	return nil, nil
}

func (o *OperationsServer) GetOperation(ctx context.Context, req *lrPb.GetOperationRequest) (*lrPb.Operation, error) {
	return nil, nil
}

func (o *OperationsServer) DeleteOperation(ctx context.Context, req *lrPb.DeleteOperationRequest) (*emptyPb.Empty, error) {
	return nil, nil
}

func (o *OperationsServer) CancelOperation(ctx context.Context, req *lrPb.CancelOperationRequest) (*emptyPb.Empty, error) {
	return nil, nil
}

func (o *OperationsServer) WaitOperation(ctx context.Context, req *lrPb.WaitOperationRequest) (*lrPb.Operation, error) {
	return nil, nil
}
