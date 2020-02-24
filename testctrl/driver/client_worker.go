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

type clientWorker struct {
	address    string
	connection *grpc.ClientConn
	client     proto.WorkerServiceClient
	scenario   *proto.Scenario
	stream     proto.WorkerService_RunClientClient
}

func NewClientWorker(address string) (Worker, error) {
	worker := new(clientWorker)
	connection, err := grpc.Dial(address, grpc.WithInsecure())
	if err != nil {
		return nil, fmt.Errorf("Unable to connect to client worker: %v", err)
	}
	worker.connection = connection
	worker.client = proto.NewWorkerServiceClient(connection)
	worker.address = address
	return worker, nil
}

func (s *clientWorker) GetAddress() string {
	return s.address
}

func (s *clientWorker) GetResponse() (interface{}, error) {
	status, err := s.stream.Recv()
	if err != nil {
		return nil, err
	}
	return status, nil
}

func (s *clientWorker) GetScenario() *proto.Scenario {
	return s.scenario
}

func (s *clientWorker) Start() error {
	stream, err := s.client.RunClient(context.Background())
	s.stream = stream
	return err
}

func (s *clientWorker) SendScenario(scenario *proto.Scenario) {
	s.scenario = scenario

	setupArgs := &proto.ClientArgs{
		Argtype: &proto.ClientArgs_Setup{
			Setup: scenario.ClientConfig,
		},
	}

	s.stream.Send(setupArgs)
}

func (s *clientWorker) Warmup() {
	s.sendResetMark()
}

func (s *clientWorker) Run() {
	s.sendResetMark()
}

func (s *clientWorker) Finalize() {
	s.sendResetMark()
}

func (s *clientWorker) Close() {
	s.stream.CloseSend()
	if _, err := s.client.QuitWorker(context.Background(), &proto.Void{}); err != nil {
		log.Printf("Failed to quit client worker safely, relying on K8s cleanup: %v\n", err)
	}
	s.connection.Close()
}

func (s *clientWorker) getResetMark() *proto.ClientArgs {
	return &proto.ClientArgs{
		Argtype: &proto.ClientArgs_Mark{
			Mark: &proto.Mark{
				Reset_: true,
			},
		},
	}
}

func (s *clientWorker) sendResetMark() {
	s.stream.Send(s.getResetMark())
}
