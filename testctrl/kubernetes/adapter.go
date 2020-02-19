package kubernetes

import (
	"context"

	appsv1 "k8s.io/api/apps/v1"
	apiv1 "k8s.io/api/core/v1"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
)

// Adapter provides a simpler interface for Kubernetes, as well as, in-pod credential management.
// Ideally, one adapter should be created at startup and shared as a singleton.
//
// The adapter can be created using a literal, but it will lack access credentials until the
// ConnectWithinCluster method is invoked.
type Adapter struct {
	clientset *kubernetes.Clientset
}

// ConnectWithinCluster uses Kubernetes utility functions to locate the credentials within the
// cluster and connect to the API. Once called, it will be authenticated and authorized for all
// subsequent calls.
func (a *Adapter) ConnectWithinCluster() error {
	config, err := rest.InClusterConfig()
	if err != nil {
		return err
	}

	clientset, err := kubernetes.NewForConfig(config)
	if err != nil {
		return err
	}

	a.clientset = clientset
	return nil
}

// CreateDeployment sends a deployment spec to the Kubernetes API.
//
// *NOTE*: Context is the first parameter, but unused at present. We are using Kubernetes Go Client
// v0.17.3 (stable), but the master branch introduces a context to the deploymentsClient.Create
// call in their examples.  This argument ensures forward compatibility once master is stable. For
// now, an implementation can provide `context.Background()`.
func (a *Adapter) CreateDeployment(_ context.Context, d *appsv1.Deployment) (*appsv1.Deployment, error) {
	deploymentsClient := a.clientset.AppsV1().Deployments(apiv1.NamespaceDefault)
	deployment, err := deploymentsClient.Create(d)
	if err != nil {
		return nil, err
	}
	return deployment, nil
}

