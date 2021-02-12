package shared

import (
	"reflect"

	"github.com/golang/protobuf/proto"
)

func extractVal(r proto.Message) reflect.Value {
	val := reflect.ValueOf(r)

	if val.Kind() == reflect.Interface {
		val = val.Elem()
	}

	if val.Kind() == reflect.Ptr {
		val = val.Elem()
	}

	return val
}

// Has returns true if the provided Message has the a field fld.
func Has(msg proto.Message, fld string) bool {
	val := extractVal(msg)
	return val.IsValid() &&
		val.FieldByName(fld).IsValid()
}
