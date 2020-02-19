package kubernetes

import (
	"strings"
	"testing"

	apiv1 "k8s.io/api/core/v1"
)

func TestContainerPorts(t *testing.T) {
	d := NewDeploymentBuilder("", ClientRole, "")
	if hasServerPort(d.ContainerPorts()) {
		t.Errorf("%s has unexpected server port", string(d.role))
	}

	d = NewDeploymentBuilder("", DriverRole, "")
	if hasServerPort(d.ContainerPorts()) {
		t.Errorf("%s has unexpected server port", string(d.role))
	}

	d = NewDeploymentBuilder("", ServerRole, "")
	if hasServerPort(d.ContainerPorts()) {
		t.Errorf("%s has unexpected server port", string(d.role))
	}
}

func hasServerPort(ports []apiv1.ContainerPort) bool {
	for _, port := range ports {
		if strings.Compare(port.Name, "serverPort") == 0 {
			return true
		}
	}
	return false
}
