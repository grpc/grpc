package driver

import (
	"context"
	"fmt"
	"log"

	"github.com/grpc/grpc/testctrl/proto"
	"google.golang.org/grpc"
)

type ClientWorker struct {
	address    string
	connection *grpc.ClientConn
	client     proto.WorkerServiceClient
	scenario   *proto.Scenario
	stream     proto.WorkerService_RunClientClient
}

func NewClientWorker(address string) (Worker, error) {
	worker := new(ClientWorker)
	connection, err := grpc.Dial(address, grpc.WithInsecure())
	if err != nil {
		return nil, fmt.Errorf("Unable to connect to client worker: %v", err)
	}
	worker.connection = connection
	worker.client = proto.NewWorkerServiceClient(connection)
	worker.address = address
	return worker, nil
}

func (s *ClientWorker) GetAddress() string {
	return s.address
}

func (s *ClientWorker) GetResponse() (interface{}, error) {
	status, err := s.stream.Recv()
	if err != nil {
		return nil, err
	}
	return status, nil
}

func (s *ClientWorker) GetScenario() *proto.Scenario {
	return s.scenario
}

func (s *ClientWorker) Start() error {
	stream, err := s.client.RunClient(context.Background())
	s.stream = stream
	return err
}

func (s *ClientWorker) SendScenario(scenario *proto.Scenario) {
	s.scenario = scenario

	setupArgs := &proto.ClientArgs{
		Argtype: &proto.ClientArgs_Setup{
			Setup: scenario.ClientConfig,
		},
	}

	s.stream.Send(setupArgs)
}

func (s *ClientWorker) Warmup() {
	s.sendResetMark()
}

func (s *ClientWorker) Run() {
	s.sendResetMark()
}

func (s *ClientWorker) Finalize() {
	s.sendResetMark()
}

func (s *ClientWorker) Close() {
	s.stream.CloseSend()
	if _, err := s.client.QuitWorker(context.Background(), &proto.Void{}); err != nil {
		log.Printf("Failed to quit client worker safely, relying on K8s cleanup: %v\n", err)
	}
	s.connection.Close()
}

func (s *ClientWorker) getResetMark() *proto.ClientArgs {
	return &proto.ClientArgs{
		Argtype: &proto.ClientArgs_Mark{
			Mark: &proto.Mark{
				Reset_: true,
			},
		},
	}
}

func (s *ClientWorker) sendResetMark() {
	s.stream.Send(s.getResetMark())
}
