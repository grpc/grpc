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
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net"
	"strings"

	"github.com/golang/protobuf/proto"
	"github.com/grpc/grpc/testctrl/driver"
	pb "github.com/grpc/grpc/testctrl/proto"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"
)

func runDriver(scenarioFilename string) {
	server, err := driver.NewServerWorker("")
	if err != nil {
		log.Fatalf("Could not connect to the server worker: %v\n", err)
	}

	client, err := driver.NewClientWorker("")
	if err != nil {
		log.Fatalf("Could not connect to the client worker: %v\n", err)
	}

	fileBody, err := ioutil.ReadFile(scenarioFilename)
	if err != nil {
		log.Fatalf("Could not read scenario file %v: %v\n", scenarioFilename, err)
	}

	stringBuilder := &strings.Builder{}
	stringBuilder.Write(fileBody)
	fmt.Printf("File body: %v\n", stringBuilder.String())

	scenario := &pb.Scenario{}
	if err := proto.Unmarshal(fileBody, scenario); err != nil {
		log.Fatalf("Scenario was malformed: %v\n", err)
	}

	result, err := driver.RunScenario(scenario, server, client)
	if err != nil {
		log.Fatalf("Scenario failed: %v\n", err)
	}

	fmt.Printf("Server results:\n%v\n\n\n\n", result.ServerStats)
	fmt.Printf("Client results:\n%v\n\n\n\n", result.ClientStats)
}

func runService(port int, enableReflection bool) {
	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", port))
	if err != nil {
		log.Fatalf("Failed to listen on port %d: %v", port, err)
	}

	grpcServer := grpc.NewServer()
	pb.RegisterOperationsServer(grpcServer, &operationsServerImpl{})
	pb.RegisterTestSessionServer(grpcServer, &testSessionsServerImpl{})

	if enableReflection {
		log.Println("Enabling reflection for grpc_cli; avoid this flag in production.")
		reflection.Register(grpcServer)
	}

	log.Printf("Running gRPC server (insecure) on port %d", port)
	err = grpcServer.Serve(lis)
	if err != nil {
		log.Fatalf("Server unexpectedly crashed: %v", err)
	}
}

func main() {
	port := flag.Int("port", 50051, "Port to start the service.")
	disableService := flag.Bool("disableService", false, "Disable gRPC service, running only a local driver.")
	enableReflection := flag.Bool("enableReflection", false, "Enable reflection to interact with grpc_cli.")
	scenario := flag.String("scenario", "", "Delete me!")
	flag.Parse()

	if *disableService {
		runDriver(*scenario)
	} else {
		runService(*port, *enableReflection)
	}
}
