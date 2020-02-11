package driver

import (
	"context"
	"fmt"
	"log"

	"github.com/codeblooded/testctrl/proto"
	"google.golang.org/grpc"
)

type ServerWorker struct {
	address    string
	connection *grpc.ClientConn
	client     proto.WorkerServiceClient
	scenario   *proto.Scenario
	stream     proto.WorkerService_RunServerClient
}

func NewServerWorker(address string) (Worker, error) {
	worker := new(ServerWorker)
	connection, err := grpc.Dial(address)
	if err != nil {
		return nil, fmt.Errorf("Unable to connect to server worker: %v", err)
	}
	worker.connection = connection
	worker.client = proto.NewWorkerServiceClient(connection)
	worker.address = address
	return worker, nil
}

func (s *ServerWorker) GetAddress() string {
	return s.address
}

func (s *ServerWorker) GetResponse() (interface{}, error) {
	status, err := s.stream.Recv()
	if err != nil {
		return nil, err
	}
	return status, nil
}

func (s *ServerWorker) GetScenario() *proto.Scenario {
	return s.scenario
}

func (s *ServerWorker) Start() error {
	stream, err := s.client.RunServer(context.Background())
	s.stream = stream
	return err
}

func (s *ServerWorker) SendScenario(scenario *proto.Scenario) {
	s.scenario = scenario

	setupArgs := &proto.ServerArgs{
		Argtype: &proto.ServerArgs_Setup{
			Setup: scenario.ServerConfig,
		},
	}

	s.stream.Send(setupArgs)
}

func (s *ServerWorker) Warmup() {
	s.sendResetMark()
}

func (s *ServerWorker) Run() {
	s.sendResetMark()
}

func (s *ServerWorker) Finalize() {
	s.sendResetMark()
}

func (s *ServerWorker) Close() {
	s.stream.CloseSend()
	if _, err := s.client.QuitWorker(context.Background(), &proto.Void{}); err != nil {
		log.Printf("Failed to quit server worker safely, relying on K8s cleanup: %v\n", err)
	}
	s.connection.Close()
}

func (s *ServerWorker) getResetMark() *proto.ServerArgs {
	return &proto.ServerArgs{
		Argtype: &proto.ServerArgs_Mark{
			Mark: &proto.Mark{
				Reset_: true,
			},
		},
	}
}

func (s *ServerWorker) sendResetMark() {
	s.stream.Send(s.getResetMark())
}
