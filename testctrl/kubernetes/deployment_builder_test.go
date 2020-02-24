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

