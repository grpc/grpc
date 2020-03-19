package orch

import (
	"time"

	"k8s.io/client-go/kubernetes"

	"github.com/grpc/grpc/testctrl/svc/types"
)

// Controller manages active and idle sessions, as well as, communications with the Kubernetes API.
type Controller struct {
	clientset   *kubernetes.Clientset
}

// NewController constructs a Controller instance with a Kubernetes Clientset. This allows the
// controller to communicate with the Kubernetes API.
func NewController(clientset *kubernetes.Clientset) *Controller {
	c := &Controller{clientset}
	return c
}

// Schedule adds a session to the controller's queue. It will remain in the queue until there are
// sufficient resources for processing and monitoring.
func (c *Controller) Schedule(s *types.Session) error {
	return nil
}

// Start spawns goroutines to monitor the Kubernetes cluster for updates and to process a limited
// number of sessions at a time.
func (c *Controller) Start() error {
	return nil
}

// Stop attempts to terminate all orchestration goroutines spawned by a call to Start. It waits for
// executors to exit. Then, it kills the kubernetes watcher.
//
// If the timeout is reached before executors exit, an error is returned. The kubernetes watcher is
// still terminated. Any sessions running on the unterminated executors will likely fail.
func (c *Controller) Stop(timeout time.Duration) error {
	return nil
}

