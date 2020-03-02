package types

import (
	"fmt"
	"time"

	pb "github.com/grpc/grpc/testctrl/genproto/testctrl/svc"
)

// Event represents an action at a specific point in time on a subject. Likely, the subject is a
// Session or Component. Events are designed to be chained together, creating a log of all
// significant points in time.
//
// Event instances can be created through the constructor or through a literal. The constructor,
// NewEvent, provides several conveniences.  First, it sets the Time to time.Now. Second, it accepts
// a Namer for the subject. This allows events to be created using the syntactic sugar of subject
// objects themselves.
type Event struct {
	// SubjectName is the string representation of the object this event describes. For example, a
	// specific session "xyz" may have the subject name of "testSessions/xyz". The format of the
	// string is implementation specific.
	SubjectName string

	// Kind is the type of event.
	Kind EventKind

	// Time is the point in time when the event was noticed.
	Time time.Time

	// Message is additional, unstructured information in a string. For example, it may describe
	// inputs and and the error message with a ErrorEvent.
	Message string
}

// NewEvent instantiates an Event struct, setting its Timestamp to now and its SubjectName to the
// result of Name method on the Namer. It expects a message, since supplying one can provide more
// context in an unstructured format.
func NewEvent(subject Namer, k EventKind, messageFmt string, args ...interface{}) Event {
	return Event{
		SubjectName: subject.Name(),
		Kind:        k,
		Time:        time.Now(),
		Message:     fmt.Sprintf(messageFmt, args...),
	}
}

// EventKind specifies the type of event. For example, an event could be created for a fatal error or
// a session is waiting in the queue.
type EventKind int32

const (
	// InternalEvent represents a problem in the infrastructure, service or controller itself. It does
	// not pertain to a session or component. If encountered, file a bug with its particular message.
	InternalEvent EventKind = iota

	// QueueEvent signals that a session is waiting for resources to run.
	QueueEvent

	// AcceptEvent indicates that an executor has been assigned to provision and monitor the session;
	// however, work has not yet begun on the event's subject.
	AcceptEvent

	// ProvisionEvent means executors have begun reserving and configuring the subject. This may take
	// a while to complete.
	ProvisionEvent

	// RunEvent indicates the subject is responding with a healthy signal. However, this does not
	// verify that it is running the tests.
	RunEvent

	// DoneEvent conveys the subject has terminated as expected. It does not indicate that the tests
	// were successful or results were recorded.
	DoneEvent

	// ErrorEvent means something has gone irrecoverably wrong with the event's subject.
	ErrorEvent
)

// EventKindFromProto takes the generated protobuf enum type and safely converts it into an EventKind.
// It ensures the string representations are equivalent, even if values change.
func EventKindFromProto(p pb.Event_Kind) EventKind {
	return eventKindStringToConstMap[p.String()]
}

// String returns the string representation of the EventKind.
func (k EventKind) String() string {
	return eventKindConstToStringMap[k]
}

// Proto converts the EventKind enum into the generated protobuf enum. It ensures the string
// representations are equivalent, even if values change.
func (k EventKind) Proto() pb.Event_Kind {
	return pb.Event_Kind(pb.Event_Kind_value[k.String()])
}

var eventKindStringToConstMap = map[string]EventKind{
	"INTERNAL":  InternalEvent,
	"QUEUE":     QueueEvent,
	"ACCEPT":    AcceptEvent,
	"PROVISION": ProvisionEvent,
	"RUN":       RunEvent,
	"DONE":      DoneEvent,
	"ERROR":     ErrorEvent,
}

var eventKindConstToStringMap = map[EventKind]string{
	InternalEvent:  "INTERNAL",
	QueueEvent:     "QUEUE",
	AcceptEvent:    "ACCEPT",
	ProvisionEvent: "PROVISION",
	RunEvent:       "RUN",
	DoneEvent:      "DONE",
	ErrorEvent:     "ERROR",
}

// EventRecorder implementations save events to a storage medium.
type EventRecorder interface {
	// Record saves an event.
	Record(e Event)
}
