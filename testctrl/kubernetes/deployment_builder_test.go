package kubernetes

import (
	"testing"
)

func TestContainerPorts(t *testing.T) {
	for _, e := range []struct{
		role DeploymentRole
		needsServerPort bool
	}{
		{ClientRole, false},
		{DriverRole, false},
		{ServerRole, true},
	}{
		d := NewDeploymentBuilder("", e.role, "")
		ports := d.ContainerPorts()

		serverPortFound := false
		for _, port := range ports {
			if port.Name == "serverPort" {
				serverPortFound = true
			}
		}

		if serverPortFound != e.needsServerPort {
			negationExpr := ""
			if !e.needsServerPort {
				negationExpr = "not "
			}
			t.Errorf("expected %s pod ports to %scontain a missing \"serverPort\"", string(d.role), negationExpr)
		}
	}
}

