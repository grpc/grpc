package types

// ResourceNamer is any type that can assign a string that identifies a specific resource.
type ResourceNamer interface {
	// ResourceName returns a string with a name for its receiver.
	ResourceName() string
}
