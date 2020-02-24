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

package driver

import (
	"context"
	"fmt"
	"log"

	"github.com/grpc/grpc/testctrl/proto"
	"google.golang.org/grpc"
)

type serverWorker struct {
	address    string
	connection *grpc.ClientConn
	client     proto.WorkerServiceClient
	scenario   *proto.Scenario
	stream     proto.WorkerService_RunServerClient
}

func NewServerWorker(address string) (Worker, error) {
	worker := new(serverWorker)
	connection, err := grpc.Dial(address, grpc.WithInsecure())
	if err != nil {
		return nil, fmt.Errorf("Unable to connect to server worker: %v", err)
	}
	worker.connection = connection
	worker.client = proto.NewWorkerServiceClient(connection)
	worker.address = address
	return worker, nil
}

func (s *serverWorker) GetAddress() string {
	return s.address
}

func (s *serverWorker) GetResponse() (interface{}, error) {
	status, err := s.stream.Recv()
	if err != nil {
		return nil, err
	}
	return status, nil
}

func (s *serverWorker) GetScenario() *proto.Scenario {
	return s.scenario
}

func (s *serverWorker) Start() error {
	stream, err := s.client.RunServer(context.Background())
	s.stream = stream
	return err
}

func (s *serverWorker) SendScenario(scenario *proto.Scenario) {
	s.scenario = scenario

	setupArgs := &proto.ServerArgs{
		Argtype: &proto.ServerArgs_Setup{
			Setup: scenario.ServerConfig,
		},
	}

	s.stream.Send(setupArgs)
}

func (s *serverWorker) Warmup() {
	s.sendResetMark()
}

func (s *serverWorker) Run() {
	s.sendResetMark()
}

func (s *serverWorker) Finalize() {
	s.sendResetMark()
}

func (s *serverWorker) Close() {
	s.stream.CloseSend()
	if _, err := s.client.QuitWorker(context.Background(), &proto.Void{}); err != nil {
		log.Printf("Failed to quit server worker safely, relying on K8s cleanup: %v\n", err)
	}
	s.connection.Close()
}

func (s *serverWorker) getResetMark() *proto.ServerArgs {
	return &proto.ServerArgs{
		Argtype: &proto.ServerArgs_Mark{
			Mark: &proto.Mark{
				Reset_: true,
			},
		},
	}
}

func (s *serverWorker) sendResetMark() {
	s.stream.Send(s.getResetMark())
}
