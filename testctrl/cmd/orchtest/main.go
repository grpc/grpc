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
			driver := types.NewComponent(*driver, types.DriverComponent, 1)
			server := types.NewComponent(*server, types.ServerComponent, 1)
			client := types.NewComponent(*client, types.ClientComponent, 1)
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
