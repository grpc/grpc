package module

import (
	"reflect"
	"regexp"
	"time"
	"unicode/utf8"

	"github.com/envoyproxy/protoc-gen-validate/validate"
	"github.com/golang/protobuf/proto"
	"github.com/golang/protobuf/ptypes"
	"github.com/golang/protobuf/ptypes/duration"
	"github.com/golang/protobuf/ptypes/timestamp"
	"github.com/lyft/protoc-gen-star"
)

var unknown = ""
var httpHeaderName = "^:?[0-9a-zA-Z!#$%&'*+-.^_|~\x60]+$"
var httpHeaderValue = "^[^\u0000-\u0008\u000A-\u001F\u007F]*$"
var headerString = "^[^\u0000\u000A\u000D]*$" // For non-strict validation.

// Map from well known regex to regex pattern.
var regex_map = map[string]*string{
	"UNKNOWN":           &unknown,
	"HTTP_HEADER_NAME":  &httpHeaderName,
	"HTTP_HEADER_VALUE": &httpHeaderValue,
	"HEADER_STRING":     &headerString,
}

type FieldType interface {
	ProtoType() pgs.ProtoType
	Embed() pgs.Message
}

type Repeatable interface {
	IsRepeated() bool
}

func (m *Module) CheckRules(msg pgs.Message) {
	m.Push("msg: " + msg.Name().String())
	defer m.Pop()

	var disabled bool
	_, err := msg.Extension(validate.E_Disabled, &disabled)
	m.CheckErr(err, "unable to read validation extension from message")

	if disabled {
		m.Debug("validation disabled, skipping checks")
		return
	}

	for _, f := range msg.Fields() {
		m.Push(f.Name().String())

		var rules validate.FieldRules
		_, err = f.Extension(validate.E_Rules, &rules)
		m.CheckErr(err, "unable to read validation rules from field")

		if rules.GetMessage() != nil {
			m.MustType(f.Type(), pgs.MessageT, pgs.UnknownWKT)
			m.CheckMessage(f, &rules)
		}

		m.CheckFieldRules(f.Type(), &rules)

		m.Pop()
	}
}

func (m *Module) CheckFieldRules(typ FieldType, rules *validate.FieldRules) {
	if rules == nil {
		return
	}

	switch r := rules.Type.(type) {
	case *validate.FieldRules_Float:
		m.MustType(typ, pgs.FloatT, pgs.FloatValueWKT)
		m.CheckFloat(r.Float)
	case *validate.FieldRules_Double:
		m.MustType(typ, pgs.DoubleT, pgs.DoubleValueWKT)
		m.CheckDouble(r.Double)
	case *validate.FieldRules_Int32:
		m.MustType(typ, pgs.Int32T, pgs.Int32ValueWKT)
		m.CheckInt32(r.Int32)
	case *validate.FieldRules_Int64:
		m.MustType(typ, pgs.Int64T, pgs.Int64ValueWKT)
		m.CheckInt64(r.Int64)
	case *validate.FieldRules_Uint32:
		m.MustType(typ, pgs.UInt32T, pgs.UInt32ValueWKT)
		m.CheckUInt32(r.Uint32)
	case *validate.FieldRules_Uint64:
		m.MustType(typ, pgs.UInt64T, pgs.UInt64ValueWKT)
		m.CheckUInt64(r.Uint64)
	case *validate.FieldRules_Sint32:
		m.MustType(typ, pgs.SInt32, pgs.UnknownWKT)
		m.CheckSInt32(r.Sint32)
	case *validate.FieldRules_Sint64:
		m.MustType(typ, pgs.SInt64, pgs.UnknownWKT)
		m.CheckSInt64(r.Sint64)
	case *validate.FieldRules_Fixed32:
		m.MustType(typ, pgs.Fixed32T, pgs.UnknownWKT)
		m.CheckFixed32(r.Fixed32)
	case *validate.FieldRules_Fixed64:
		m.MustType(typ, pgs.Fixed64T, pgs.UnknownWKT)
		m.CheckFixed64(r.Fixed64)
	case *validate.FieldRules_Sfixed32:
		m.MustType(typ, pgs.SFixed32, pgs.UnknownWKT)
		m.CheckSFixed32(r.Sfixed32)
	case *validate.FieldRules_Sfixed64:
		m.MustType(typ, pgs.SFixed64, pgs.UnknownWKT)
		m.CheckSFixed64(r.Sfixed64)
	case *validate.FieldRules_Bool:
		m.MustType(typ, pgs.BoolT, pgs.BoolValueWKT)
	case *validate.FieldRules_String_:
		m.MustType(typ, pgs.StringT, pgs.StringValueWKT)
		m.CheckString(r.String_)
	case *validate.FieldRules_Bytes:
		m.MustType(typ, pgs.BytesT, pgs.BytesValueWKT)
		m.CheckBytes(r.Bytes)
	case *validate.FieldRules_Enum:
		m.MustType(typ, pgs.EnumT, pgs.UnknownWKT)
		m.CheckEnum(typ, r.Enum)
	case *validate.FieldRules_Repeated:
		m.CheckRepeated(typ, r.Repeated)
	case *validate.FieldRules_Map:
		m.CheckMap(typ, r.Map)
	case *validate.FieldRules_Any:
		m.CheckAny(typ, r.Any)
	case *validate.FieldRules_Duration:
		m.CheckDuration(typ, r.Duration)
	case *validate.FieldRules_Timestamp:
		m.CheckTimestamp(typ, r.Timestamp)
	case nil: // noop
	default:
		m.Failf("unknown rule type (%T)", rules.Type)
	}
}

func (m *Module) MustType(typ FieldType, pt pgs.ProtoType, wrapper pgs.WellKnownType) {
	if emb := typ.Embed(); emb != nil && emb.IsWellKnown() && emb.WellKnownType() == wrapper {
		m.MustType(emb.Fields()[0].Type(), pt, pgs.UnknownWKT)
		return
	}

	if typ, ok := typ.(Repeatable); ok {
		m.Assert(!typ.IsRepeated(),
			"repeated rule should be used for repeated fields")
	}

	m.Assert(typ.ProtoType() == pt,
		" expected rules for ",
		typ.ProtoType().Proto(),
		" but got ",
		pt.Proto(),
	)
}

func (m *Module) CheckFloat(r *validate.FloatRules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckDouble(r *validate.DoubleRules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckInt32(r *validate.Int32Rules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckInt64(r *validate.Int64Rules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckUInt32(r *validate.UInt32Rules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckUInt64(r *validate.UInt64Rules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckSInt32(r *validate.SInt32Rules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckSInt64(r *validate.SInt64Rules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckFixed32(r *validate.Fixed32Rules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckFixed64(r *validate.Fixed64Rules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckSFixed32(r *validate.SFixed32Rules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckSFixed64(r *validate.SFixed64Rules) {
	m.checkNums(len(r.In), len(r.NotIn), r.Const, r.Lt, r.Lte, r.Gt, r.Gte)
}

func (m *Module) CheckString(r *validate.StringRules) {
	m.checkLen(r.Len, r.MinLen, r.MaxLen)
	m.checkLen(r.LenBytes, r.MinBytes, r.MaxBytes)
	m.checkMinMax(r.MinLen, r.MaxLen)
	m.checkMinMax(r.MinBytes, r.MaxBytes)
	m.checkIns(len(r.In), len(r.NotIn))
	m.checkWellKnownRegex(r.GetWellKnownRegex(), r)
	m.checkPattern(r.Pattern, len(r.In))

	if r.MaxLen != nil {
		max := int(r.GetMaxLen())
		m.Assert(utf8.RuneCountInString(r.GetPrefix()) <= max, "`prefix` length exceeds the `max_len`")
		m.Assert(utf8.RuneCountInString(r.GetSuffix()) <= max, "`suffix` length exceeds the `max_len`")
		m.Assert(utf8.RuneCountInString(r.GetContains()) <= max, "`contains` length exceeds the `max_len`")

		m.Assert(
			r.MaxBytes == nil || r.GetMaxBytes() >= r.GetMaxLen(),
			"`max_len` cannot exceed `max_bytes`")
	}

	if r.MaxBytes != nil {
		max := int(r.GetMaxBytes())
		m.Assert(len(r.GetPrefix()) <= max, "`prefix` length exceeds the `max_bytes`")
		m.Assert(len(r.GetSuffix()) <= max, "`suffix` length exceeds the `max_bytes`")
		m.Assert(len(r.GetContains()) <= max, "`contains` length exceeds the `max_bytes`")
	}
}

func (m *Module) CheckBytes(r *validate.BytesRules) {
	m.checkMinMax(r.MinLen, r.MaxLen)
	m.checkIns(len(r.In), len(r.NotIn))
	m.checkPattern(r.Pattern, len(r.In))

	if r.MaxLen != nil {
		max := int(r.GetMaxLen())
		m.Assert(len(r.GetPrefix()) <= max, "`prefix` length exceeds the `max_len`")
		m.Assert(len(r.GetSuffix()) <= max, "`suffix` length exceeds the `max_len`")
		m.Assert(len(r.GetContains()) <= max, "`contains` length exceeds the `max_len`")
	}
}

func (m *Module) CheckEnum(ft FieldType, r *validate.EnumRules) {
	m.checkIns(len(r.In), len(r.NotIn))

	if r.GetDefinedOnly() && len(r.In) > 0 {
		typ, ok := ft.(interface {
			Enum() pgs.Enum
		})

		if !ok {
			m.Failf("unexpected field type (%T)", ft)
		}

		defined := typ.Enum().Values()
		vals := make(map[int32]struct{}, len(defined))

		for _, val := range defined {
			vals[val.Value()] = struct{}{}
		}

		for _, in := range r.In {
			if _, ok = vals[in]; !ok {
				m.Failf("undefined `in` value (%d) conflicts with `defined_only` rule")
			}
		}
	}
}

func (m *Module) CheckMessage(f pgs.Field, rules *validate.FieldRules) {
	m.Assert(f.Type().IsEmbed(), "field is not embedded but got message rules")
	emb := f.Type().Embed()
	if emb != nil && emb.IsWellKnown() {
		switch emb.WellKnownType() {
		case pgs.AnyWKT:
			m.Failf("Any rules should be used for Any fields")
		case pgs.DurationWKT:
			m.Failf("Duration rules should be used for Duration fields")
		case pgs.TimestampWKT:
			m.Failf("Timestamp rules should be used for Timestamp fields")
		}
	}

	if rules.Type != nil && rules.GetMessage().GetSkip() {
		m.Failf("Skip should not be used with WKT scalar rules")
	}
}

func (m *Module) CheckRepeated(ft FieldType, r *validate.RepeatedRules) {
	typ := m.mustFieldType(ft)

	m.Assert(typ.IsRepeated(), "field is not repeated but got repeated rules")

	m.checkMinMax(r.MinItems, r.MaxItems)

	if r.GetUnique() {
		m.Assert(
			!typ.Element().IsEmbed(),
			"unique rule is only applicable for scalar types")
	}

	m.Push("items")
	m.CheckFieldRules(typ.Element(), r.Items)
	m.Pop()
}

func (m *Module) CheckMap(ft FieldType, r *validate.MapRules) {
	typ := m.mustFieldType(ft)

	m.Assert(typ.IsMap(), "field is not a map but got map rules")

	m.checkMinMax(r.MinPairs, r.MaxPairs)

	if r.GetNoSparse() {
		m.Assert(
			typ.Element().IsEmbed(),
			"no_sparse rule is only applicable for embedded message types",
		)
	}

	m.Push("keys")
	m.CheckFieldRules(typ.Key(), r.Keys)
	m.Pop()

	m.Push("values")
	m.CheckFieldRules(typ.Element(), r.Values)
	m.Pop()
}

func (m *Module) CheckAny(ft FieldType, r *validate.AnyRules) {
	m.checkIns(len(r.In), len(r.NotIn))
}

func (m *Module) CheckDuration(ft FieldType, r *validate.DurationRules) {
	m.checkNums(
		len(r.GetIn()),
		len(r.GetNotIn()),
		m.checkDur(r.GetConst()),
		m.checkDur(r.GetLt()),
		m.checkDur(r.GetLte()),
		m.checkDur(r.GetGt()),
		m.checkDur(r.GetGte()))

	for _, v := range r.GetIn() {
		m.Assert(v != nil, "cannot have nil values in `in`")
		m.checkDur(v)
	}

	for _, v := range r.GetNotIn() {
		m.Assert(v != nil, "cannot have nil values in `not_in`")
		m.checkDur(v)
	}
}

func (m *Module) CheckTimestamp(ft FieldType, r *validate.TimestampRules) {
	m.checkNums(0, 0,
		m.checkTS(r.GetConst()),
		m.checkTS(r.GetLt()),
		m.checkTS(r.GetLte()),
		m.checkTS(r.GetGt()),
		m.checkTS(r.GetGte()))

	m.Assert(
		(r.LtNow == nil && r.GtNow == nil) || (r.Lt == nil && r.Lte == nil && r.Gt == nil && r.Gte == nil),
		"`now` rules cannot be mixed with absolute `lt/gt` rules")

	m.Assert(
		r.Within == nil || (r.Lt == nil && r.Lte == nil && r.Gt == nil && r.Gte == nil),
		"`within` rule cannot be used with absolute `lt/gt` rules")

	m.Assert(
		r.LtNow == nil || r.GtNow == nil,
		"both `now` rules cannot be used together")

	dur := m.checkDur(r.Within)
	m.Assert(
		dur == nil || *dur > 0,
		"`within` rule must be positive and non-zero")
}

func (m *Module) mustFieldType(ft FieldType) pgs.FieldType {
	typ, ok := ft.(pgs.FieldType)
	if !ok {
		m.Failf("unexpected field type (%T)", ft)
	}

	return typ
}

func (m *Module) checkNums(in, notIn int, ci, lti, ltei, gti, gtei interface{}) {
	m.checkIns(in, notIn)

	c := reflect.ValueOf(ci)
	lt, lte := reflect.ValueOf(lti), reflect.ValueOf(ltei)
	gt, gte := reflect.ValueOf(gti), reflect.ValueOf(gtei)

	m.Assert(
		c.IsNil() ||
			in == 0 && notIn == 0 &&
				lt.IsNil() && lte.IsNil() &&
				gt.IsNil() && gte.IsNil(),
		"`const` can be the only rule on a field",
	)

	m.Assert(
		in == 0 ||
			lt.IsNil() && lte.IsNil() &&
				gt.IsNil() && gte.IsNil(),
		"cannot have both `in` and range constraint rules on the same field",
	)

	m.Assert(
		lt.IsNil() || lte.IsNil(),
		"cannot have both `lt` and `lte` rules on the same field",
	)

	m.Assert(
		gt.IsNil() || gte.IsNil(),
		"cannot have both `gt` and `gte` rules on the same field",
	)

	if !lt.IsNil() {
		m.Assert(gt.IsNil() || !reflect.DeepEqual(lti, gti),
			"cannot have equal `gt` and `lt` rules on the same field")
		m.Assert(gte.IsNil() || !reflect.DeepEqual(lti, gtei),
			"cannot have equal `gte` and `lt` rules on the same field")
	} else if !lte.IsNil() {
		m.Assert(gt.IsNil() || !reflect.DeepEqual(ltei, gti),
			"cannot have equal `gt` and `lte` rules on the same field")
		m.Assert(gte.IsNil() || !reflect.DeepEqual(ltei, gtei),
			"use `const` instead of equal `lte` and `gte` rules")
	}
}

func (m *Module) checkIns(in, notIn int) {
	m.Assert(
		in == 0 || notIn == 0,
		"cannot have both `in` and `not_in` rules on the same field")
}

func (m *Module) checkMinMax(min, max *uint64) {
	if min == nil || max == nil {
		return
	}

	m.Assert(
		*min <= *max,
		"`min` value is greater than `max` value")
}

func (m *Module) checkLen(len, min, max *uint64) {
	if len == nil {
		return
	}

	m.Assert(
		min == nil,
		"cannot have both `len` and `min_len` rules on the same field")

	m.Assert(
		max == nil,
		"cannot have both `len` and `max_len` rules on the same field")
}

func (m *Module) checkWellKnownRegex(wk validate.KnownRegex, r *validate.StringRules) {
	if wk != 0 {
		m.Assert(r.Pattern == nil, "regex `well_known_regex` and regex `pattern` are incompatible")
		var non_strict = r.Strict != nil && *r.Strict == false
		if (wk.String() == "HTTP_HEADER_NAME" || wk.String() == "HTTP_HEADER_VALUE") && non_strict {
			// Use non-strict header validation.
			r.Pattern = regex_map["HEADER_STRING"]
		} else {
			r.Pattern = regex_map[wk.String()]
		}
	}
}

func (m *Module) checkPattern(p *string, in int) {
	if p != nil {
		m.Assert(in == 0, "regex `pattern` and `in` rules are incompatible")
		_, err := regexp.Compile(*p)
		m.CheckErr(err, "unable to parse regex `pattern`")
	}
}

func (m *Module) checkDur(d *duration.Duration) *time.Duration {
	if d == nil {
		return nil
	}

	dur, err := ptypes.Duration(d)
	m.CheckErr(err, "could not resolve duration")
	return &dur
}

func (m *Module) checkTS(ts *timestamp.Timestamp) *int64 {
	if ts == nil {
		return nil
	}

	t, err := ptypes.Timestamp(ts)
	m.CheckErr(err, "could not resolve timestamp")
	return proto.Int64(t.UnixNano())
}
