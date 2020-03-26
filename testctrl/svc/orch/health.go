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
	// Unknown indicates that the state of the object could not be mapped to a standard health
	// value. The system should wait until another health value is set.
	Unknown Health = iota

	// Ready indicates that an object is healthy and available.
	Ready

	// NotReady indicates that something is not yet correct with an object's state, but this
	// may be recoverable.
	NotReady

	// Succeeded indicates that an object has terminated successfully.
	Succeeded

	// Failed indicates that an object has terminated due to a failure.
	Failed
)

// String returns the string representation of a health constant.
func (h Health) String() string {
	return healthConstToStringMap[h]
}

var healthConstToStringMap = map[Health]string{
	Unknown: "Unknown",
	Ready: "Ready",
	NotReady: "NotReady",
	Succeeded: "Succeeded",
	Failed: "Failed",
}
