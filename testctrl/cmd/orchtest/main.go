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
	"log"
	"os"
	"time"

	"github.com/golang/glog"
	"github.com/golang/protobuf/jsonpb"

	grpcPb "github.com/codeblooded/grpc-proto/genproto/grpc/testing"
	"github.com/grpc/grpc/testctrl/auth"
	"github.com/grpc/grpc/testctrl/svc/orch"
	"github.com/grpc/grpc/testctrl/svc/types"
)

func main() {
	driver := flag.String("driver", "", "Container image of a driver for testing")
	server := flag.String("server", "", "Container image of a server for testing")
	client := flag.String("client", "", "Container image of a client for testing")
	timeout := flag.Duration("timeout", 5*time.Minute, "Allow the controller to live for this duration")
	scenarioJSON := flag.String("scenarioJSON", "", "Scenario protobuf with test config as a JSON object")
	count := flag.Int("count", 1, "Number of sessions to schedule")

	flag.Parse()
	defer glog.Flush()

	config, set := os.LookupEnv("KUBE_CONFIG_FILE")
	if !set {
		glog.Fatalln("Missing a kube config file, specify its absolute path in the KUBE_CONFIG_FILE env variable.")
	}
	clientset, err := auth.ConnectWithConfig(config)
	if err != nil {
		glog.Fatalf("Invalid config file specified by the KUBE_CONFIG_FILE env variable, unable to connect: %v", err)
	}

	c := orch.NewController(clientset)
	if err := c.Start(); err != nil {
		panic(err)
	}
	defer c.Stop(*timeout)

	go func() {
		for i := 0; i < *count; i++ {
			driver := types.NewComponent(*driver, types.DriverComponent)
			server := types.NewComponent(*server, types.ServerComponent)
			client := types.NewComponent(*client, types.ClientComponent)
			c.Schedule(types.NewSession(driver, []*types.Component{server, client}, scenario(*scenarioJSON)))
		}
	}()

	time.Sleep(*timeout)
}

func scenario(scenarioJSON string) *grpcPb.Scenario {
	if len(scenarioJSON) == 0 {
		return nil
	}

	var s grpcPb.Scenario
	if err := jsonpb.UnmarshalString(scenarioJSON, &s); err != nil {
		log.Fatalf("could not parse scenario json: %v", err)
	}
	return &s
}
