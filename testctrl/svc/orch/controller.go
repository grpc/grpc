package orch

import (
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

// Stop safely terminates all goroutines spawned by the call to Start. It returns immediately, but
// it allows the active sessions to finish before terminating goroutines.
func (c *Controller) Stop() error {
	return nil
}

