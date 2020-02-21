package kubernetes

import (
	appsv1 "k8s.io/api/apps/v1"
	apiv1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// DeploymentRole differentiates deployments with driver, client and server pods.
type DeploymentRole string

const (
	// ClientRole indicates the deployment contains a client worker.
	ClientRole DeploymentRole = "client"

	// DriverRole indicates the deployment contains a driver.
	DriverRole                = "driver"

	// ServerRole indicates the deployment contains a server.
	ServerRole                = "server"
)

// DefaultReplicaCount is the number of replicas the default deployment creates.
const DefaultReplicaCount int32 = 1

// DefaultDriverPort is the default port for communication between a driver and workers.
const DefaultDriverPort int32 = 10000

// DefaultServerPort is the default port for communication between a server and client.
const DefaultServerPort int32 = 10010

// DeploymentBuilder creates deployment specs with consistent names and labels.
//
// It should not be created directly, because the zero values cannot produce valid deployment
// specs.  By using the constructor, sane defaults are provided for all fields.
type DeploymentBuilder struct {
	sessionID      string
	replicas       int32
	role           DeploymentRole
	containerImage string
	driverPort     int32
	serverPort     int32
}

// NewDeploymentBuilder initializes a DeploymentBuilder for creating deployment specs.
func NewDeploymentBuilder(sessionID string, role DeploymentRole, containerImage string) *DeploymentBuilder {
	return &DeploymentBuilder{
		sessionID:      sessionID,
		replicas:       DefaultReplicaCount,
		role:           role,
		containerImage: containerImage,
		driverPort:     DefaultDriverPort,
		serverPort:     DefaultServerPort,
	}
}

// SessionID returns the unique identifier for the session.
func (d *DeploymentBuilder) SessionID() string {
	return d.sessionID
}

// ReplicaCount returns the number of containers the deployment will manage.
func (d *DeploymentBuilder) ReplicaCount() int32 {
	return d.replicas
}

// SetReplicaCount changes the number of containers the deployment manages.  This is not currently
// supported for load testing scenarios.
func (d *DeploymentBuilder) SetReplicaCount(n int32) {
	d.replicas = n
}

// Role returns the DeploymentRole passed to the constructor.
func (d *DeploymentBuilder) Role() DeploymentRole {
	return d.role
}

// ContainerImage returns the URI to the container image that the deployment will manage.
func (d *DeploymentBuilder) ContainerImage() string {
	return d.containerImage
}

// DriverPort returns the port for communication between a driver and workers.
func (d *DeploymentBuilder) DriverPort() int32 {
	return d.driverPort
}

// SetDriverPort changes the port for communication between the driver and workers.  This port must
// be changed in the driver, server and client DeploymentBuilder objects.
func (d *DeploymentBuilder) SetDriverPort(p int32) {
	d.driverPort = p
}

// ServerPort returns the port for communication between a server and client.
func (d *DeploymentBuilder) ServerPort() int32 {
	return d.serverPort
}

// SetServerPort changes the port for communication between a server and client.  This port must be
// changed in both the server and client DeploymentBuilder objects.
func (d *DeploymentBuilder) SetServerPort(p int32) {
	d.serverPort = p
}

// Deployment builds a Kubernetes deployment object.
func (d *DeploymentBuilder) Deployment() *appsv1.Deployment {
	var zero int32 = 0
	return &appsv1.Deployment{
		ObjectMeta: metav1.ObjectMeta{
			Name:        d.sessionID,
			Labels:      d.Labels(),
			Annotations: d.Annotations(),
		},

		Spec: appsv1.DeploymentSpec{
			Replicas: &d.replicas,
			Selector: &metav1.LabelSelector{
				MatchLabels: d.Labels(),
			},
			Strategy: appsv1.DeploymentStrategy{
				// disable rolling updates
				Type: "Recreate",
				RollingUpdate: nil,
			},
			RevisionHistoryLimit: &zero,  // disable rollbacks
			Template: d.PodTemplateSpec(),
		},
	}
}

// PodTemplateSpec builds a Kubernetes podspec for the deployment to manage.
func (d *DeploymentBuilder) PodTemplateSpec() apiv1.PodTemplateSpec {
	return apiv1.PodTemplateSpec{
		ObjectMeta: metav1.ObjectMeta{
			Name:        d.sessionID,
			Labels:      d.Labels(),
			Annotations: d.Annotations(),
		},

		Spec: apiv1.PodSpec{
			Containers: d.Containers(),
		},
	}
}

// Containers lists the containers available in each pod the deployment manages.  Since sidecars
// are not used for load testing, there will only be one container in the returned slice.
func (d *DeploymentBuilder) Containers() []apiv1.Container {
	return []apiv1.Container{
		{
			Name:  string(d.role),
			Image: d.containerImage,
			Ports: d.ContainerPorts(),
		},
	}
}

// ContainerPorts specifies the ingress ports (TCP) on the pods.  For the driver and client, this
// is only the DriverPort. For the server, the ServerPort is included.
func (d *DeploymentBuilder) ContainerPorts() []apiv1.ContainerPort {
	var ports []apiv1.ContainerPort

	ports = append(ports, apiv1.ContainerPort{
		Name:          "driver-port",
		Protocol:      apiv1.ProtocolTCP,
		ContainerPort: d.driverPort,
	})

	if d.role == ServerRole {
		ports = append(ports, apiv1.ContainerPort{
			Name:          "server-port",
			Protocol:      apiv1.ProtocolTCP,
			ContainerPort: d.serverPort,
		})
	}

	return ports
}

// Labels constructs a map of labels that are shared by the deployment and pod.
func (d *DeploymentBuilder) Labels() map[string]string {
	return map[string]string{
		"sessionID": d.sessionID,
		"role":      string(d.role),
	}
}

// Annotations constructs a map of annotations that are shared by the deployment and pod.
func (d *DeploymentBuilder) Annotations() map[string]string {
	return map[string]string{}
}

