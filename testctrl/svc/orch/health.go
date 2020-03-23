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
