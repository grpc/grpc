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

package orch

import (
	"reflect"
	"testing"

	apiv1 "k8s.io/api/core/v1"

	"github.com/grpc/grpc/testctrl/svc/types"
	"github.com/grpc/grpc/testctrl/svc/types/test"
)

func TestSpecBuilderContainers(t *testing.T) {
	image := "debian:jessie"
	component := test.NewComponentBuilder().SetContainer(image).Build()
	session := test.NewSessionBuilder().SetComponents(component).Build()
	sb := NewSpecBuilder(session, component)
	containers := sb.Containers()

	if len(containers) < 1 {
		t.Fatalf("SpecBuilder Containers did not specify any containers; expected '%s'", image)
	}

	if actualImage := containers[0].Image; actualImage != image {
		t.Errorf("SpecBuilder Containers did not correctly set the container image; expected '%s' but got '%s'", image, actualImage)
	}
}

func TestSpecBuilderContainerPorts(t *testing.T) {
	cases := []struct {
		kind  types.ComponentKind
		ports []int32
	}{
		{types.DriverComponent, []int32{driverPort}},
		{types.ClientComponent, []int32{driverPort}},
		{types.ServerComponent, []int32{driverPort, serverPort}},
	}

	var containerPortSlice = func(cps []apiv1.ContainerPort) []int32 {
		var ports []int32

		for _, port := range cps {
			ports = append(ports, port.ContainerPort)
		}

		return ports
	}

	for _, c := range cases {
		component := test.NewComponentBuilder().SetKind(c.kind).Build()
		session := test.NewSessionBuilder().SetComponents(component).Build()
		sb := NewSpecBuilder(session, component)
		ports := containerPortSlice(sb.ContainerPorts())

		if !reflect.DeepEqual(ports, c.ports) {
			t.Errorf("SpecBuilder ContainerPorts does not contain the correct ports for %s; expected %v but got %v", c.kind, c.ports, ports)
		}
	}
}

func TestSpecBuilderDeploymentSpec(t *testing.T) {
	// Check that replicas are properly set
	component := test.NewComponentBuilder().SetReplicas(3).Build()
	session := test.NewSessionBuilder().SetComponents(component).Build()
	sb := NewSpecBuilder(session, component)

	replicaPtr := sb.DeploymentSpec().Replicas
	if replicaPtr == nil {
		t.Errorf("SpecBuilder DeploymentSpec did not specify a number of component replicas; expected '%v'", component.Replicas())
	} else if *replicaPtr != component.Replicas() {
		t.Errorf("SpecBuilder DeploymentSpec did not set the correct number of component replicas; expected '%v' but got '%v'", component.Replicas(), *replicaPtr)
	}

	// Check that the selector includes all labels
	component = test.NewComponentBuilder().Build()
	session = test.NewSessionBuilder().SetComponents(component).Build()
	sb = NewSpecBuilder(session, component)
	matchLabels := sb.DeploymentSpec().Selector.MatchLabels

	if !reflect.DeepEqual(matchLabels, sb.Labels()) {
		t.Errorf("SpecBuilder DeploymentSpec did not match correct pods on deployment; expected labels '%v' but got '%v'", sb.Labels(), matchLabels)
	}
}

func TestSpecBuilderLabels(t *testing.T) {
	// Check that spec contains a 'session-name' label
	session := test.NewSessionBuilder().Build()
	sb := NewSpecBuilder(session, test.NewComponentBuilder().Build())
	labels := sb.Labels()

	if sessionName := labels["session-name"]; sessionName != session.Name() {
		t.Errorf("SpecBuilder Labels generated incorrect 'session-name' label; expected '%s' but got '%v'", session.Name(), sessionName)
	}

	// Check the spec contains 'component-name' label
	component := test.NewComponentBuilder().Build()
	sb = NewSpecBuilder(test.NewSessionBuilder().Build(), component)
	labels = sb.Labels()

	if componentName := labels["component-name"]; componentName != component.Name() {
		t.Errorf("SpecBuilder Labels generated incorrect 'component-name' label; expected '%s' but got '%v'", component.Name(), componentName)
	}

	// Check the spec constains 'component-kind' label
	kindCases := []struct {
		kind       types.ComponentKind
		labelValue string
	}{
		{types.DriverComponent, "driver"},
		{types.ClientComponent, "client"},
		{types.ServerComponent, "server"},
	}

	for _, c := range kindCases {
		component := test.NewComponentBuilder().SetKind(c.kind).Build()
		session := test.NewSessionBuilder().SetComponents(component).Build()
		sb := NewSpecBuilder(session, component)
		labels := sb.Labels()

		if kind := labels["component-kind"]; kind != c.labelValue {
			t.Errorf("SpecBuilder Labels generated incorrect 'component-kind' label for %s component; expected '%s' but got '%v'", c.kind.String(), c.labelValue, kind)
		}
	}

	// Check that the 'autogen' label exists, signifying that this resource was automatically generated
	sb = NewSpecBuilder(test.NewSessionBuilder().Build(), test.NewComponentBuilder().Build())
	labels = sb.Labels()

	if autogen := labels["autogen"]; autogen != "1" {
		t.Errorf("SpecBuilder Labels missing 'autogen' label to signify generated component")
	}
}

func TestSpecBuilderObjectMeta(t *testing.T) {
	component := test.NewComponentBuilder().Build()
	componentName := component.Name()
	session := test.NewSessionBuilder().SetComponents(component).Build()
	sb := NewSpecBuilder(session, component)

	if resourceName := sb.ObjectMeta().Name; resourceName != componentName {
		t.Errorf("SpecBuilder ObjectMeta did not set the K8s resource name to the component name; expected '%s' but got '%s'", componentName, resourceName)
	}
}

func TestSpecBuilderEnv(t *testing.T) {
	// check all component env variables are copied to spec
	key := "TESTING"
	value := "true"
	component := test.NewComponentBuilder().SetEnv(key, value).Build()
	session := test.NewSessionBuilder().SetComponents(component).Build()
	sb := NewSpecBuilder(session, component)
	got := getEnv(sb.Env(), key)
	if got == nil || *got != value {
		t.Errorf("SpecBuilder Env did not copy all component env variables")
	}

	// check SCENARIO_JSON is always and only set on driver
	cases := []struct {
		componentKind   types.ComponentKind
		includeScenario bool
	}{
		{types.DriverComponent, true},
		{types.ServerComponent, false},
		{types.ClientComponent, false},
	}

	for _, c := range cases {
		component := test.NewComponentBuilder().SetKind(c.componentKind).Build()
		session := test.NewSessionBuilder().SetComponents(component).Build()
		sb := NewSpecBuilder(session, component)
		included := getEnv(sb.Env(), "SCENARIO_JSON") != nil

		if included != c.includeScenario {
			if c.includeScenario {
				t.Errorf("SpecBuilder Env did not set $SCENARIO_JSON env variable for %v", c.componentKind)
			} else {
				t.Errorf("SpecBuilder Env unexpectedly set $SCENARIO_JSON env variable for %v", c.componentKind)
			}
		}
	}
}

func getEnv(envs []apiv1.EnvVar, name string) *string {
	for _, env := range envs {
		if env.Name == name {
			return &env.Value
		}
	}
	return nil
}
