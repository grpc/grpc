package shared

import (
	"github.com/envoyproxy/protoc-gen-validate/validate"
	"github.com/lyft/protoc-gen-star"
)

type WellKnown string

const (
	Email    WellKnown = "email"
	Hostname WellKnown = "hostname"
	UUID     WellKnown = "uuid"
)

func FileNeeds(f pgs.File, wk WellKnown) bool {
	for _, msg := range f.Messages() {
		needed := Needs(msg, wk)
		if needed {
			return true
		}
	}
	return false
}

// Needs returns true if a well-known string validator is needed for this
// message.
func Needs(m pgs.Message, wk WellKnown) bool {

	for _, f := range m.Fields() {
		var rules validate.FieldRules
		if _, err := f.Extension(validate.E_Rules, &rules); err != nil {
			continue
		}

		switch {
		case f.Type().IsRepeated() && f.Type().Element().ProtoType() == pgs.StringT:
			if strRulesNeeds(rules.GetRepeated().GetItems().GetString_(), wk) {
				return true
			}
		case f.Type().IsMap():
			if f.Type().Key().ProtoType() == pgs.StringT &&
				strRulesNeeds(rules.GetMap().GetKeys().GetString_(), wk) {
				return true
			}
			if f.Type().Element().ProtoType() == pgs.StringT &&
				strRulesNeeds(rules.GetMap().GetValues().GetString_(), wk) {
				return true
			}
		case f.Type().ProtoType() == pgs.StringT:
			if strRulesNeeds(rules.GetString_(), wk) {
				return true
			}
		case f.Type().ProtoType() == pgs.MessageT && f.Type().IsEmbed() && f.Type().Embed().WellKnownType() == pgs.StringValueWKT:
			if strRulesNeeds(rules.GetString_(), wk) {
				return true
			}
		}
	}

	return false
}

func strRulesNeeds(rules *validate.StringRules, wk WellKnown) bool {
	switch wk {
	case Email:
		if rules.GetEmail() {
			return true
		}
	case Hostname:
		if rules.GetEmail() || rules.GetHostname() || rules.GetAddress() {
			return true
		}
	case UUID:
		if rules.GetUuid() {
			return true
		}
	}

	return false
}
