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
  "log"
  "net"
  "os"
  "strings"

  "github.com/grpc/grpc/testctrl/kubernetes"
  pb "github.com/grpc/grpc/testctrl/genproto/testctrl/svc"
  "github.com/grpc/grpc/testctrl/svc"
  lrPb "google.golang.org/genproto/googleapis/longrunning"
  "google.golang.org/grpc"
  "google.golang.org/grpc/reflection"

)

func setupProdEnv(adapter *kubernetes.Adapter) {
  if err := adapter.ConnectWithinCluster(); err != nil {
    log.Fatalf("Unable to connect to kubernetes API within cluster: %v", err)
  }
}

func setupDevEnv(adapter *kubernetes.Adapter, grpcServer *grpc.Server) {
  c, set := os.LookupEnv("KUBE_CONFIG_FILE")
  if !set {
    log.Fatalf("Missing a kube config file, specify its absolute path in the KUBE_CONFIG_FILE env variable.")
  }
  if err := adapter.ConnectWithConfig(c); err != nil {
    log.Fatalf("Invalid config file specified by the KUBE_CONFIG_FILE env variable, unable to connect: %v", err)
  }

  log.Println("Enabling reflection for grpc_cli; avoid this flag in production.")
  reflection.Register(grpcServer)
}

func main() {
  port := flag.Int("port", 50051, "Port to start the service.")
  flag.Parse()

  grpcServer := grpc.NewServer()
  adapter := &kubernetes.Adapter{}

  env := os.Getenv("APP_ENV")
  if strings.Compare(env, "production") == 0 {
    log.Println("App environment set to production")
    setupProdEnv(adapter)
  } else {
    log.Println("App environment set to development")
    setupDevEnv(adapter, grpcServer)
  }

  lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *port))
  if err != nil {
    log.Fatalf("Failed to listen on port %d: %v", *port, err)
  }

  lrPb.RegisterOperationsServer(grpcServer, &svc.OperationsServer{})
  pb.RegisterTestSessionsServer(grpcServer, &svc.TestSessionsServer{adapter})

  log.Printf("Running gRPC server (insecure) on port %d", *port)
  err = grpcServer.Serve(lis)
  if err != nil {
    log.Fatalf("Server unexpectedly crashed: %v", err)
  }
}
