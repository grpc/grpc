package driver

import (
	"fmt"
	"log"
	"time"

	"github.com/grpc/grpc/testctrl/proto"
)

// Worker is an abstraction of a test component, such as a client or server.
// This interface allows them to be used interchangeably with implementation
// details hidden.
//
// This interface is also useful for testing without a network or mocks. The
// tests can implement versions of the worker that produce expected results
// and use dependency injection.
type Worker interface {
	// GetAddress returns a string with the host and port number of a worker,
	// delimited by a colon.  For example, `127.0.0.1:4041`.
	//
	// Note this may return an empty string if the worker has not been assigned
	// an address or connected to one.  This depends on the Worker
	// implementation.
	GetAddress() string

	// GetResponse blocks on the next streamed value from the worker and returns
	// the result or an error.
	GetResponse() (interface{}, error)

	// GetScenario returns the current scenario under test.  It is set when
	// SendScenario() is called with a valid scenario protobuf.  The return
	// value will always be the argument to SendScenario(), even if the workers
	// have not successfully acknowledged the send. The return value will be
	// nil if SendScenario() has not been called.
	GetScenario() *proto.Scenario

	// Start invokes the appropriate RPCs on a worker to begin accepting or
	// sending traffic for the requests.
	Start() error

	// SendScenario sends the appropriate configuration to a worker.  For
	// example, the server worker will receive the `ServerConfig` and the client
	// will receive the `ClientConfig` over RPC.
	SendScenario(scenario *proto.Scenario)

	// Warmup allows the workers to begin sending or receiving traffic for a
	// specific allotment of time.  This should be invoked before a call to
	// Run() which actually collects metrics.  It is designed to remove outliers
	// due to system processes, like JVM startup.
	Warmup()

	// Run tells the worker to begin measuring while it accepts or receives
	// traffic; in effect, running the tests.
	Run()

	// Finalize sends a ping that the worker should stop and stream its results.
	Finalize()

	// Close prevents the worker from streaming any more data and gracefully
	// shuts it down.  This method is designed to be deferred.
	Close()
}

// ScenarioResults encompass the results of the server and client.
type ScenarioResults struct {
	// ServerStats is a protobuf with the server statistics after a scenario run.
	ServerStats *proto.ServerStats

	// ClientStats is a protobuf with the client statistics after a scenario run.
	ClientStats *proto.ClientStats
}

// RunScenario runs a singular test scenario on a single server and client
// implementation of the Worker interface. It does provide any analysis or
// preprocessing but returns *ScenarioResults upon success.  With any
// foreseen failures, it does not panic.  It relies on error checking to
// determine if it has successfully completed.
func RunScenario(scenario *proto.Scenario, server, client Worker) (*ScenarioResults, error) {
	log.Println("Calling start RPCs on server and client workers.")
	if err := server.Start(); err != nil {
		log.Printf("Failed to run server: %v\n", err)
		return nil, err
	}
	if err := client.Start(); err != nil {
		log.Printf("Failed to run client: %v\n", err)
		return nil, err
	}
	defer server.Close()
	defer client.Close()

	log.Println("Sending scenario to server and client workers.")
	server.SendScenario(scenario)
	if _, err := server.GetResponse(); err != nil {
		log.Printf("Sent config setup args to server, but failed to receive adequate reply: %v\n", err)
		return nil, err
	}
	client.SendScenario(scenario)
	if _, err := client.GetResponse(); err != nil {
		log.Printf("Sent config setup args to client, but failed to receive adequate reply: %v\n", err)
		return nil, err
	}

	log.Println("Sending mark to warmup server and client workers.")
	server.Warmup()
	if _, err := server.GetResponse(); err != nil {
		log.Printf("Sent warmup mark to server, but failed to receive adequate reply: %v\n", err)
		return nil, err
	}
	client.Warmup()
	if _, err := server.GetResponse(); err != nil {
		log.Printf("Sent warmup mark to client, but failed to receive adequate reply: %v\n", err)
		return nil, err
	}

	log.Printf("Driver going to sleep for %ds to allow server and client to warmup\n", scenario.WarmupSeconds)
	warmupDuration, err := time.ParseDuration(fmt.Sprintf("%ds", scenario.WarmupSeconds))
	if err != nil {
		log.Printf("Failed to parse warmup duration (%d seconds): %v\n", scenario.WarmupSeconds, err)
		return nil, err
	}
	time.Sleep(warmupDuration)

	log.Printf("Sending mark to begin tests which will run for %ds\n", scenario.BenchmarkSeconds)
	server.Run()
	if _, err := server.GetResponse(); err != nil {
		log.Printf("Sent run mark to server, but failed to receive adequate reply: %v\n", err)
		return nil, err
	}
	client.Run()
	if _, err := client.GetResponse(); err != nil {
		log.Printf("Sent run mark to client, but failed to receive adequate reply: %v\n", err)
		return nil, err
	}

	// NOTE: This diverges from the C++ implementation which waits for scenario.warmup_seconds + scenario.benchmark_seconds
	// for the tests to finish.  For clarity, I just use the benchmark seconds here.  It's an easy fix, however.
	log.Printf("Driver going to sleep for %ds to allow server and client to run tests\n", scenario.BenchmarkSeconds)
	benchmarkDuration, err := time.ParseDuration(fmt.Sprintf("%ds", scenario.BenchmarkSeconds))
	if err != nil {
		log.Printf("Failed to parse benchmark duration (%d seconds): %v\n", scenario.BenchmarkSeconds, err)
		return nil, err
	}
	time.Sleep(benchmarkDuration)

	log.Println("Sending mark to collect final results")
	server.Finalize()
	serverResults, err := server.GetResponse()
	if err != nil {
		log.Printf("Sent mark to collect final results to server, but failed to receive adequate reply: %v\n", err)
		return nil, err
	}
	client.Finalize()
	clientResults, err := client.GetResponse()
	if err != nil {
		log.Printf("Sent mark to collect final results to client, but failed to receive adequate reply: %v\n", err)
		return nil, err
	}

	log.Println("Closing server and client connections.")
	return &ScenarioResults{
		ServerStats: serverResults.(*proto.ServerStatus).Stats,
		ClientStats: clientResults.(*proto.ClientStatus).Stats,
	}, nil
}
