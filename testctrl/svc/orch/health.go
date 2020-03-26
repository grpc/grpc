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

// Health indicates the availability or readiness of an object or a set of objects.
type Health int32

const (
	// Unknown indicates the health status has not been updated.
	Unknown Health = iota

	// Unhealthy indicates that the object is not available due an error.
	Unhealthy

	// Healthy indicates the object is available and appears to be running.
	Healthy

	// Done indicates the object has terminated with a successful state.
	Done

	// Failed indicates the object has terminated in an unsuccessful state.
	Failed
)

// String returns the string representation of a health constant.
func (h Health) String() string {
	return healthConstToStringMap[h]
}

var healthConstToStringMap = map[Health]string{
	Unknown:   "unknown",
	Unhealthy: "unhealthy",
	Healthy:   "healthy",
	Done:      "done",
	Failed:    "failed",
}
