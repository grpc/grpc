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

package main

import (
	"context"
	"fmt"
	"log"

	"github.com/grpc/grpc/testctrl/kubernetes"
	pb "github.com/grpc/grpc/testctrl/proto"
	"github.com/google/uuid"
	appsv1 "k8s.io/api/apps/v1"
)

type testSessionsServerImpl struct{
	adapter *kubernetes.Adapter
}

func (t *testSessionsServerImpl) StartTestSession(ctx context.Context, req *pb.StartTestSessionRequest) (*pb.Operation, error) {
	sessionID := uuid.New().String()

	containerImageMap := map[kubernetes.DeploymentRole]string{
		kubernetes.ClientRole: req.WorkerContainerImage,
		kubernetes.DriverRole: req.DriverContainerImage,
		kubernetes.ServerRole: req.WorkerContainerImage,
	}

	for role, image := range containerImageMap {
		d := kubernetes.NewDeploymentBuilder(sessionID, role, image).Deployment()

		d, err := t.adapter.CreateDeployment(context.Background(), d)
		if err != nil {
			log.Printf("Deployment of %s for session %s failed: %v", string(role), sessionID, err)
			// TODO: ADD CLEAN UP LOGIC TO REMOVE OTHER SESSION DEPLOYMENTS
			break
		}

		log.Printf("Deployment of %s for session %s succeeded", string(role), sessionID)
	}

	operation := new(pb.Operation)
	operation.Name = fmt.Sprintf("testSessions/%s", sessionID)
	operation.Done = false
	return operation, nil
}
