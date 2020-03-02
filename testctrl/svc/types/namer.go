package types

// Namer is any type that can assign a string that identifies a specific instance.
type Namer interface {
	// Name returns a string with a name for its receiver.
	Name() string
}
