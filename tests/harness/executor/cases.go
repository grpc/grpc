package main

import (
	"math"

	"time"

	cases "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/go"
	other_package "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/other_package/go"
	"github.com/golang/protobuf/proto"
	"github.com/golang/protobuf/ptypes"
	"github.com/golang/protobuf/ptypes/any"
	"github.com/golang/protobuf/ptypes/duration"
	"github.com/golang/protobuf/ptypes/timestamp"
	"github.com/golang/protobuf/ptypes/wrappers"
)

type TestCase struct {
	Name    string
	Message proto.Message
	Valid   bool
}

type TestResult struct {
	OK, Skipped bool
}

var TestCases []TestCase

func init() {
	sets := [][]TestCase{
		floatCases,
		doubleCases,
		int32Cases,
		int64Cases,
		uint32Cases,
		uint64Cases,
		sint32Cases,
		sint64Cases,
		fixed32Cases,
		fixed64Cases,
		sfixed32Cases,
		sfixed64Cases,
		boolCases,
		stringCases,
		bytesCases,
		enumCases,
		messageCases,
		repeatedCases,
		mapCases,
		oneofCases,
		wrapperCases,
		durationCases,
		timestampCases,
		anyCases,
		kitchenSink,
	}

	for _, set := range sets {
		TestCases = append(TestCases, set...)
	}
}

var floatCases = []TestCase{
	{"float - none - valid", &cases.FloatNone{Val: -1.23456}, true},

	{"float - const - valid", &cases.FloatConst{Val: 1.23}, true},
	{"float - const - invalid", &cases.FloatConst{Val: 4.56}, false},

	{"float - in - valid", &cases.FloatIn{Val: 7.89}, true},
	{"float - in - invalid", &cases.FloatIn{Val: 10.11}, false},

	{"float - not in - valid", &cases.FloatNotIn{Val: 1}, true},
	{"float - not in - invalid", &cases.FloatNotIn{Val: 0}, false},

	{"float - lt - valid", &cases.FloatLT{Val: -1}, true},
	{"float - lt - invalid (equal)", &cases.FloatLT{Val: 0}, false},
	{"float - lt - invalid", &cases.FloatLT{Val: 1}, false},

	{"float - lte - valid", &cases.FloatLTE{Val: 63}, true},
	{"float - lte - valid (equal)", &cases.FloatLTE{Val: 64}, true},
	{"float - lte - invalid", &cases.FloatLTE{Val: 65}, false},

	{"float - gt - valid", &cases.FloatGT{Val: 17}, true},
	{"float - gt - invalid (equal)", &cases.FloatGT{Val: 16}, false},
	{"float - gt - invalid", &cases.FloatGT{Val: 15}, false},

	{"float - gte - valid", &cases.FloatGTE{Val: 9}, true},
	{"float - gte - valid (equal)", &cases.FloatGTE{Val: 8}, true},
	{"float - gte - invalid", &cases.FloatGTE{Val: 7}, false},

	{"float - gt & lt - valid", &cases.FloatGTLT{Val: 5}, true},
	{"float - gt & lt - invalid (above)", &cases.FloatGTLT{Val: 11}, false},
	{"float - gt & lt - invalid (below)", &cases.FloatGTLT{Val: -1}, false},
	{"float - gt & lt - invalid (max)", &cases.FloatGTLT{Val: 10}, false},
	{"float - gt & lt - invalid (min)", &cases.FloatGTLT{Val: 0}, false},

	{"float - exclusive gt & lt - valid (above)", &cases.FloatExLTGT{Val: 11}, true},
	{"float - exclusive gt & lt - valid (below)", &cases.FloatExLTGT{Val: -1}, true},
	{"float - exclusive gt & lt - invalid", &cases.FloatExLTGT{Val: 5}, false},
	{"float - exclusive gt & lt - invalid (max)", &cases.FloatExLTGT{Val: 10}, false},
	{"float - exclusive gt & lt - invalid (min)", &cases.FloatExLTGT{Val: 0}, false},

	{"float - gte & lte - valid", &cases.FloatGTELTE{Val: 200}, true},
	{"float - gte & lte - valid (max)", &cases.FloatGTELTE{Val: 256}, true},
	{"float - gte & lte - valid (min)", &cases.FloatGTELTE{Val: 128}, true},
	{"float - gte & lte - invalid (above)", &cases.FloatGTELTE{Val: 300}, false},
	{"float - gte & lte - invalid (below)", &cases.FloatGTELTE{Val: 100}, false},

	{"float - exclusive gte & lte - valid (above)", &cases.FloatExGTELTE{Val: 300}, true},
	{"float - exclusive gte & lte - valid (below)", &cases.FloatExGTELTE{Val: 100}, true},
	{"float - exclusive gte & lte - valid (max)", &cases.FloatExGTELTE{Val: 256}, true},
	{"float - exclusive gte & lte - valid (min)", &cases.FloatExGTELTE{Val: 128}, true},
	{"float - exclusive gte & lte - invalid", &cases.FloatExGTELTE{Val: 200}, false},
}

var doubleCases = []TestCase{
	{"double - none - valid", &cases.DoubleNone{Val: -1.23456}, true},

	{"double - const - valid", &cases.DoubleConst{Val: 1.23}, true},
	{"double - const - invalid", &cases.DoubleConst{Val: 4.56}, false},

	{"double - in - valid", &cases.DoubleIn{Val: 7.89}, true},
	{"double - in - invalid", &cases.DoubleIn{Val: 10.11}, false},

	{"double - not in - valid", &cases.DoubleNotIn{Val: 1}, true},
	{"double - not in - invalid", &cases.DoubleNotIn{Val: 0}, false},

	{"double - lt - valid", &cases.DoubleLT{Val: -1}, true},
	{"double - lt - invalid (equal)", &cases.DoubleLT{Val: 0}, false},
	{"double - lt - invalid", &cases.DoubleLT{Val: 1}, false},

	{"double - lte - valid", &cases.DoubleLTE{Val: 63}, true},
	{"double - lte - valid (equal)", &cases.DoubleLTE{Val: 64}, true},
	{"double - lte - invalid", &cases.DoubleLTE{Val: 65}, false},

	{"double - gt - valid", &cases.DoubleGT{Val: 17}, true},
	{"double - gt - invalid (equal)", &cases.DoubleGT{Val: 16}, false},
	{"double - gt - invalid", &cases.DoubleGT{Val: 15}, false},

	{"double - gte - valid", &cases.DoubleGTE{Val: 9}, true},
	{"double - gte - valid (equal)", &cases.DoubleGTE{Val: 8}, true},
	{"double - gte - invalid", &cases.DoubleGTE{Val: 7}, false},

	{"double - gt & lt - valid", &cases.DoubleGTLT{Val: 5}, true},
	{"double - gt & lt - invalid (above)", &cases.DoubleGTLT{Val: 11}, false},
	{"double - gt & lt - invalid (below)", &cases.DoubleGTLT{Val: -1}, false},
	{"double - gt & lt - invalid (max)", &cases.DoubleGTLT{Val: 10}, false},
	{"double - gt & lt - invalid (min)", &cases.DoubleGTLT{Val: 0}, false},

	{"double - exclusive gt & lt - valid (above)", &cases.DoubleExLTGT{Val: 11}, true},
	{"double - exclusive gt & lt - valid (below)", &cases.DoubleExLTGT{Val: -1}, true},
	{"double - exclusive gt & lt - invalid", &cases.DoubleExLTGT{Val: 5}, false},
	{"double - exclusive gt & lt - invalid (max)", &cases.DoubleExLTGT{Val: 10}, false},
	{"double - exclusive gt & lt - invalid (min)", &cases.DoubleExLTGT{Val: 0}, false},

	{"double - gte & lte - valid", &cases.DoubleGTELTE{Val: 200}, true},
	{"double - gte & lte - valid (max)", &cases.DoubleGTELTE{Val: 256}, true},
	{"double - gte & lte - valid (min)", &cases.DoubleGTELTE{Val: 128}, true},
	{"double - gte & lte - invalid (above)", &cases.DoubleGTELTE{Val: 300}, false},
	{"double - gte & lte - invalid (below)", &cases.DoubleGTELTE{Val: 100}, false},

	{"double - exclusive gte & lte - valid (above)", &cases.DoubleExGTELTE{Val: 300}, true},
	{"double - exclusive gte & lte - valid (below)", &cases.DoubleExGTELTE{Val: 100}, true},
	{"double - exclusive gte & lte - valid (max)", &cases.DoubleExGTELTE{Val: 256}, true},
	{"double - exclusive gte & lte - valid (min)", &cases.DoubleExGTELTE{Val: 128}, true},
	{"double - exclusive gte & lte - invalid", &cases.DoubleExGTELTE{Val: 200}, false},
}

var int32Cases = []TestCase{
	{"int32 - none - valid", &cases.Int32None{Val: 123}, true},

	{"int32 - const - valid", &cases.Int32Const{Val: 1}, true},
	{"int32 - const - invalid", &cases.Int32Const{Val: 2}, false},

	{"int32 - in - valid", &cases.Int32In{Val: 3}, true},
	{"int32 - in - invalid", &cases.Int32In{Val: 5}, false},

	{"int32 - not in - valid", &cases.Int32NotIn{Val: 1}, true},
	{"int32 - not in - invalid", &cases.Int32NotIn{Val: 0}, false},

	{"int32 - lt - valid", &cases.Int32LT{Val: -1}, true},
	{"int32 - lt - invalid (equal)", &cases.Int32LT{Val: 0}, false},
	{"int32 - lt - invalid", &cases.Int32LT{Val: 1}, false},

	{"int32 - lte - valid", &cases.Int32LTE{Val: 63}, true},
	{"int32 - lte - valid (equal)", &cases.Int32LTE{Val: 64}, true},
	{"int32 - lte - invalid", &cases.Int32LTE{Val: 65}, false},

	{"int32 - gt - valid", &cases.Int32GT{Val: 17}, true},
	{"int32 - gt - invalid (equal)", &cases.Int32GT{Val: 16}, false},
	{"int32 - gt - invalid", &cases.Int32GT{Val: 15}, false},

	{"int32 - gte - valid", &cases.Int32GTE{Val: 9}, true},
	{"int32 - gte - valid (equal)", &cases.Int32GTE{Val: 8}, true},
	{"int32 - gte - invalid", &cases.Int32GTE{Val: 7}, false},

	{"int32 - gt & lt - valid", &cases.Int32GTLT{Val: 5}, true},
	{"int32 - gt & lt - invalid (above)", &cases.Int32GTLT{Val: 11}, false},
	{"int32 - gt & lt - invalid (below)", &cases.Int32GTLT{Val: -1}, false},
	{"int32 - gt & lt - invalid (max)", &cases.Int32GTLT{Val: 10}, false},
	{"int32 - gt & lt - invalid (min)", &cases.Int32GTLT{Val: 0}, false},

	{"int32 - exclusive gt & lt - valid (above)", &cases.Int32ExLTGT{Val: 11}, true},
	{"int32 - exclusive gt & lt - valid (below)", &cases.Int32ExLTGT{Val: -1}, true},
	{"int32 - exclusive gt & lt - invalid", &cases.Int32ExLTGT{Val: 5}, false},
	{"int32 - exclusive gt & lt - invalid (max)", &cases.Int32ExLTGT{Val: 10}, false},
	{"int32 - exclusive gt & lt - invalid (min)", &cases.Int32ExLTGT{Val: 0}, false},

	{"int32 - gte & lte - valid", &cases.Int32GTELTE{Val: 200}, true},
	{"int32 - gte & lte - valid (max)", &cases.Int32GTELTE{Val: 256}, true},
	{"int32 - gte & lte - valid (min)", &cases.Int32GTELTE{Val: 128}, true},
	{"int32 - gte & lte - invalid (above)", &cases.Int32GTELTE{Val: 300}, false},
	{"int32 - gte & lte - invalid (below)", &cases.Int32GTELTE{Val: 100}, false},

	{"int32 - exclusive gte & lte - valid (above)", &cases.Int32ExGTELTE{Val: 300}, true},
	{"int32 - exclusive gte & lte - valid (below)", &cases.Int32ExGTELTE{Val: 100}, true},
	{"int32 - exclusive gte & lte - valid (max)", &cases.Int32ExGTELTE{Val: 256}, true},
	{"int32 - exclusive gte & lte - valid (min)", &cases.Int32ExGTELTE{Val: 128}, true},
	{"int32 - exclusive gte & lte - invalid", &cases.Int32ExGTELTE{Val: 200}, false},
}

var int64Cases = []TestCase{
	{"int64 - none - valid", &cases.Int64None{Val: 123}, true},

	{"int64 - const - valid", &cases.Int64Const{Val: 1}, true},
	{"int64 - const - invalid", &cases.Int64Const{Val: 2}, false},

	{"int64 - in - valid", &cases.Int64In{Val: 3}, true},
	{"int64 - in - invalid", &cases.Int64In{Val: 5}, false},

	{"int64 - not in - valid", &cases.Int64NotIn{Val: 1}, true},
	{"int64 - not in - invalid", &cases.Int64NotIn{Val: 0}, false},

	{"int64 - lt - valid", &cases.Int64LT{Val: -1}, true},
	{"int64 - lt - invalid (equal)", &cases.Int64LT{Val: 0}, false},
	{"int64 - lt - invalid", &cases.Int64LT{Val: 1}, false},

	{"int64 - lte - valid", &cases.Int64LTE{Val: 63}, true},
	{"int64 - lte - valid (equal)", &cases.Int64LTE{Val: 64}, true},
	{"int64 - lte - invalid", &cases.Int64LTE{Val: 65}, false},

	{"int64 - gt - valid", &cases.Int64GT{Val: 17}, true},
	{"int64 - gt - invalid (equal)", &cases.Int64GT{Val: 16}, false},
	{"int64 - gt - invalid", &cases.Int64GT{Val: 15}, false},

	{"int64 - gte - valid", &cases.Int64GTE{Val: 9}, true},
	{"int64 - gte - valid (equal)", &cases.Int64GTE{Val: 8}, true},
	{"int64 - gte - invalid", &cases.Int64GTE{Val: 7}, false},

	{"int64 - gt & lt - valid", &cases.Int64GTLT{Val: 5}, true},
	{"int64 - gt & lt - invalid (above)", &cases.Int64GTLT{Val: 11}, false},
	{"int64 - gt & lt - invalid (below)", &cases.Int64GTLT{Val: -1}, false},
	{"int64 - gt & lt - invalid (max)", &cases.Int64GTLT{Val: 10}, false},
	{"int64 - gt & lt - invalid (min)", &cases.Int64GTLT{Val: 0}, false},

	{"int64 - exclusive gt & lt - valid (above)", &cases.Int64ExLTGT{Val: 11}, true},
	{"int64 - exclusive gt & lt - valid (below)", &cases.Int64ExLTGT{Val: -1}, true},
	{"int64 - exclusive gt & lt - invalid", &cases.Int64ExLTGT{Val: 5}, false},
	{"int64 - exclusive gt & lt - invalid (max)", &cases.Int64ExLTGT{Val: 10}, false},
	{"int64 - exclusive gt & lt - invalid (min)", &cases.Int64ExLTGT{Val: 0}, false},

	{"int64 - gte & lte - valid", &cases.Int64GTELTE{Val: 200}, true},
	{"int64 - gte & lte - valid (max)", &cases.Int64GTELTE{Val: 256}, true},
	{"int64 - gte & lte - valid (min)", &cases.Int64GTELTE{Val: 128}, true},
	{"int64 - gte & lte - invalid (above)", &cases.Int64GTELTE{Val: 300}, false},
	{"int64 - gte & lte - invalid (below)", &cases.Int64GTELTE{Val: 100}, false},

	{"int64 - exclusive gte & lte - valid (above)", &cases.Int64ExGTELTE{Val: 300}, true},
	{"int64 - exclusive gte & lte - valid (below)", &cases.Int64ExGTELTE{Val: 100}, true},
	{"int64 - exclusive gte & lte - valid (max)", &cases.Int64ExGTELTE{Val: 256}, true},
	{"int64 - exclusive gte & lte - valid (min)", &cases.Int64ExGTELTE{Val: 128}, true},
	{"int64 - exclusive gte & lte - invalid", &cases.Int64ExGTELTE{Val: 200}, false},
}

var uint32Cases = []TestCase{
	{"uint32 - none - valid", &cases.UInt32None{Val: 123}, true},

	{"uint32 - const - valid", &cases.UInt32Const{Val: 1}, true},
	{"uint32 - const - invalid", &cases.UInt32Const{Val: 2}, false},

	{"uint32 - in - valid", &cases.UInt32In{Val: 3}, true},
	{"uint32 - in - invalid", &cases.UInt32In{Val: 5}, false},

	{"uint32 - not in - valid", &cases.UInt32NotIn{Val: 1}, true},
	{"uint32 - not in - invalid", &cases.UInt32NotIn{Val: 0}, false},

	{"uint32 - lt - valid", &cases.UInt32LT{Val: 4}, true},
	{"uint32 - lt - invalid (equal)", &cases.UInt32LT{Val: 5}, false},
	{"uint32 - lt - invalid", &cases.UInt32LT{Val: 6}, false},

	{"uint32 - lte - valid", &cases.UInt32LTE{Val: 63}, true},
	{"uint32 - lte - valid (equal)", &cases.UInt32LTE{Val: 64}, true},
	{"uint32 - lte - invalid", &cases.UInt32LTE{Val: 65}, false},

	{"uint32 - gt - valid", &cases.UInt32GT{Val: 17}, true},
	{"uint32 - gt - invalid (equal)", &cases.UInt32GT{Val: 16}, false},
	{"uint32 - gt - invalid", &cases.UInt32GT{Val: 15}, false},

	{"uint32 - gte - valid", &cases.UInt32GTE{Val: 9}, true},
	{"uint32 - gte - valid (equal)", &cases.UInt32GTE{Val: 8}, true},
	{"uint32 - gte - invalid", &cases.UInt32GTE{Val: 7}, false},

	{"uint32 - gt & lt - valid", &cases.UInt32GTLT{Val: 7}, true},
	{"uint32 - gt & lt - invalid (above)", &cases.UInt32GTLT{Val: 11}, false},
	{"uint32 - gt & lt - invalid (below)", &cases.UInt32GTLT{Val: 1}, false},
	{"uint32 - gt & lt - invalid (max)", &cases.UInt32GTLT{Val: 10}, false},
	{"uint32 - gt & lt - invalid (min)", &cases.UInt32GTLT{Val: 5}, false},

	{"uint32 - exclusive gt & lt - valid (above)", &cases.UInt32ExLTGT{Val: 11}, true},
	{"uint32 - exclusive gt & lt - valid (below)", &cases.UInt32ExLTGT{Val: 4}, true},
	{"uint32 - exclusive gt & lt - invalid", &cases.UInt32ExLTGT{Val: 7}, false},
	{"uint32 - exclusive gt & lt - invalid (max)", &cases.UInt32ExLTGT{Val: 10}, false},
	{"uint32 - exclusive gt & lt - invalid (min)", &cases.UInt32ExLTGT{Val: 5}, false},

	{"uint32 - gte & lte - valid", &cases.UInt32GTELTE{Val: 200}, true},
	{"uint32 - gte & lte - valid (max)", &cases.UInt32GTELTE{Val: 256}, true},
	{"uint32 - gte & lte - valid (min)", &cases.UInt32GTELTE{Val: 128}, true},
	{"uint32 - gte & lte - invalid (above)", &cases.UInt32GTELTE{Val: 300}, false},
	{"uint32 - gte & lte - invalid (below)", &cases.UInt32GTELTE{Val: 100}, false},

	{"uint32 - exclusive gte & lte - valid (above)", &cases.UInt32ExGTELTE{Val: 300}, true},
	{"uint32 - exclusive gte & lte - valid (below)", &cases.UInt32ExGTELTE{Val: 100}, true},
	{"uint32 - exclusive gte & lte - valid (max)", &cases.UInt32ExGTELTE{Val: 256}, true},
	{"uint32 - exclusive gte & lte - valid (min)", &cases.UInt32ExGTELTE{Val: 128}, true},
	{"uint32 - exclusive gte & lte - invalid", &cases.UInt32ExGTELTE{Val: 200}, false},
}

var uint64Cases = []TestCase{
	{"uint64 - none - valid", &cases.UInt64None{Val: 123}, true},

	{"uint64 - const - valid", &cases.UInt64Const{Val: 1}, true},
	{"uint64 - const - invalid", &cases.UInt64Const{Val: 2}, false},

	{"uint64 - in - valid", &cases.UInt64In{Val: 3}, true},
	{"uint64 - in - invalid", &cases.UInt64In{Val: 5}, false},

	{"uint64 - not in - valid", &cases.UInt64NotIn{Val: 1}, true},
	{"uint64 - not in - invalid", &cases.UInt64NotIn{Val: 0}, false},

	{"uint64 - lt - valid", &cases.UInt64LT{Val: 4}, true},
	{"uint64 - lt - invalid (equal)", &cases.UInt64LT{Val: 5}, false},
	{"uint64 - lt - invalid", &cases.UInt64LT{Val: 6}, false},

	{"uint64 - lte - valid", &cases.UInt64LTE{Val: 63}, true},
	{"uint64 - lte - valid (equal)", &cases.UInt64LTE{Val: 64}, true},
	{"uint64 - lte - invalid", &cases.UInt64LTE{Val: 65}, false},

	{"uint64 - gt - valid", &cases.UInt64GT{Val: 17}, true},
	{"uint64 - gt - invalid (equal)", &cases.UInt64GT{Val: 16}, false},
	{"uint64 - gt - invalid", &cases.UInt64GT{Val: 15}, false},

	{"uint64 - gte - valid", &cases.UInt64GTE{Val: 9}, true},
	{"uint64 - gte - valid (equal)", &cases.UInt64GTE{Val: 8}, true},
	{"uint64 - gte - invalid", &cases.UInt64GTE{Val: 7}, false},

	{"uint64 - gt & lt - valid", &cases.UInt64GTLT{Val: 7}, true},
	{"uint64 - gt & lt - invalid (above)", &cases.UInt64GTLT{Val: 11}, false},
	{"uint64 - gt & lt - invalid (below)", &cases.UInt64GTLT{Val: 1}, false},
	{"uint64 - gt & lt - invalid (max)", &cases.UInt64GTLT{Val: 10}, false},
	{"uint64 - gt & lt - invalid (min)", &cases.UInt64GTLT{Val: 5}, false},

	{"uint64 - exclusive gt & lt - valid (above)", &cases.UInt64ExLTGT{Val: 11}, true},
	{"uint64 - exclusive gt & lt - valid (below)", &cases.UInt64ExLTGT{Val: 4}, true},
	{"uint64 - exclusive gt & lt - invalid", &cases.UInt64ExLTGT{Val: 7}, false},
	{"uint64 - exclusive gt & lt - invalid (max)", &cases.UInt64ExLTGT{Val: 10}, false},
	{"uint64 - exclusive gt & lt - invalid (min)", &cases.UInt64ExLTGT{Val: 5}, false},

	{"uint64 - gte & lte - valid", &cases.UInt64GTELTE{Val: 200}, true},
	{"uint64 - gte & lte - valid (max)", &cases.UInt64GTELTE{Val: 256}, true},
	{"uint64 - gte & lte - valid (min)", &cases.UInt64GTELTE{Val: 128}, true},
	{"uint64 - gte & lte - invalid (above)", &cases.UInt64GTELTE{Val: 300}, false},
	{"uint64 - gte & lte - invalid (below)", &cases.UInt64GTELTE{Val: 100}, false},

	{"uint64 - exclusive gte & lte - valid (above)", &cases.UInt64ExGTELTE{Val: 300}, true},
	{"uint64 - exclusive gte & lte - valid (below)", &cases.UInt64ExGTELTE{Val: 100}, true},
	{"uint64 - exclusive gte & lte - valid (max)", &cases.UInt64ExGTELTE{Val: 256}, true},
	{"uint64 - exclusive gte & lte - valid (min)", &cases.UInt64ExGTELTE{Val: 128}, true},
	{"uint64 - exclusive gte & lte - invalid", &cases.UInt64ExGTELTE{Val: 200}, false},
}

var sint32Cases = []TestCase{
	{"sint32 - none - valid", &cases.SInt32None{Val: 123}, true},

	{"sint32 - const - valid", &cases.SInt32Const{Val: 1}, true},
	{"sint32 - const - invalid", &cases.SInt32Const{Val: 2}, false},

	{"sint32 - in - valid", &cases.SInt32In{Val: 3}, true},
	{"sint32 - in - invalid", &cases.SInt32In{Val: 5}, false},

	{"sint32 - not in - valid", &cases.SInt32NotIn{Val: 1}, true},
	{"sint32 - not in - invalid", &cases.SInt32NotIn{Val: 0}, false},

	{"sint32 - lt - valid", &cases.SInt32LT{Val: -1}, true},
	{"sint32 - lt - invalid (equal)", &cases.SInt32LT{Val: 0}, false},
	{"sint32 - lt - invalid", &cases.SInt32LT{Val: 1}, false},

	{"sint32 - lte - valid", &cases.SInt32LTE{Val: 63}, true},
	{"sint32 - lte - valid (equal)", &cases.SInt32LTE{Val: 64}, true},
	{"sint32 - lte - invalid", &cases.SInt32LTE{Val: 65}, false},

	{"sint32 - gt - valid", &cases.SInt32GT{Val: 17}, true},
	{"sint32 - gt - invalid (equal)", &cases.SInt32GT{Val: 16}, false},
	{"sint32 - gt - invalid", &cases.SInt32GT{Val: 15}, false},

	{"sint32 - gte - valid", &cases.SInt32GTE{Val: 9}, true},
	{"sint32 - gte - valid (equal)", &cases.SInt32GTE{Val: 8}, true},
	{"sint32 - gte - invalid", &cases.SInt32GTE{Val: 7}, false},

	{"sint32 - gt & lt - valid", &cases.SInt32GTLT{Val: 5}, true},
	{"sint32 - gt & lt - invalid (above)", &cases.SInt32GTLT{Val: 11}, false},
	{"sint32 - gt & lt - invalid (below)", &cases.SInt32GTLT{Val: -1}, false},
	{"sint32 - gt & lt - invalid (max)", &cases.SInt32GTLT{Val: 10}, false},
	{"sint32 - gt & lt - invalid (min)", &cases.SInt32GTLT{Val: 0}, false},

	{"sint32 - exclusive gt & lt - valid (above)", &cases.SInt32ExLTGT{Val: 11}, true},
	{"sint32 - exclusive gt & lt - valid (below)", &cases.SInt32ExLTGT{Val: -1}, true},
	{"sint32 - exclusive gt & lt - invalid", &cases.SInt32ExLTGT{Val: 5}, false},
	{"sint32 - exclusive gt & lt - invalid (max)", &cases.SInt32ExLTGT{Val: 10}, false},
	{"sint32 - exclusive gt & lt - invalid (min)", &cases.SInt32ExLTGT{Val: 0}, false},

	{"sint32 - gte & lte - valid", &cases.SInt32GTELTE{Val: 200}, true},
	{"sint32 - gte & lte - valid (max)", &cases.SInt32GTELTE{Val: 256}, true},
	{"sint32 - gte & lte - valid (min)", &cases.SInt32GTELTE{Val: 128}, true},
	{"sint32 - gte & lte - invalid (above)", &cases.SInt32GTELTE{Val: 300}, false},
	{"sint32 - gte & lte - invalid (below)", &cases.SInt32GTELTE{Val: 100}, false},

	{"sint32 - exclusive gte & lte - valid (above)", &cases.SInt32ExGTELTE{Val: 300}, true},
	{"sint32 - exclusive gte & lte - valid (below)", &cases.SInt32ExGTELTE{Val: 100}, true},
	{"sint32 - exclusive gte & lte - valid (max)", &cases.SInt32ExGTELTE{Val: 256}, true},
	{"sint32 - exclusive gte & lte - valid (min)", &cases.SInt32ExGTELTE{Val: 128}, true},
	{"sint32 - exclusive gte & lte - invalid", &cases.SInt32ExGTELTE{Val: 200}, false},
}

var sint64Cases = []TestCase{
	{"sint64 - none - valid", &cases.SInt64None{Val: 123}, true},

	{"sint64 - const - valid", &cases.SInt64Const{Val: 1}, true},
	{"sint64 - const - invalid", &cases.SInt64Const{Val: 2}, false},

	{"sint64 - in - valid", &cases.SInt64In{Val: 3}, true},
	{"sint64 - in - invalid", &cases.SInt64In{Val: 5}, false},

	{"sint64 - not in - valid", &cases.SInt64NotIn{Val: 1}, true},
	{"sint64 - not in - invalid", &cases.SInt64NotIn{Val: 0}, false},

	{"sint64 - lt - valid", &cases.SInt64LT{Val: -1}, true},
	{"sint64 - lt - invalid (equal)", &cases.SInt64LT{Val: 0}, false},
	{"sint64 - lt - invalid", &cases.SInt64LT{Val: 1}, false},

	{"sint64 - lte - valid", &cases.SInt64LTE{Val: 63}, true},
	{"sint64 - lte - valid (equal)", &cases.SInt64LTE{Val: 64}, true},
	{"sint64 - lte - invalid", &cases.SInt64LTE{Val: 65}, false},

	{"sint64 - gt - valid", &cases.SInt64GT{Val: 17}, true},
	{"sint64 - gt - invalid (equal)", &cases.SInt64GT{Val: 16}, false},
	{"sint64 - gt - invalid", &cases.SInt64GT{Val: 15}, false},

	{"sint64 - gte - valid", &cases.SInt64GTE{Val: 9}, true},
	{"sint64 - gte - valid (equal)", &cases.SInt64GTE{Val: 8}, true},
	{"sint64 - gte - invalid", &cases.SInt64GTE{Val: 7}, false},

	{"sint64 - gt & lt - valid", &cases.SInt64GTLT{Val: 5}, true},
	{"sint64 - gt & lt - invalid (above)", &cases.SInt64GTLT{Val: 11}, false},
	{"sint64 - gt & lt - invalid (below)", &cases.SInt64GTLT{Val: -1}, false},
	{"sint64 - gt & lt - invalid (max)", &cases.SInt64GTLT{Val: 10}, false},
	{"sint64 - gt & lt - invalid (min)", &cases.SInt64GTLT{Val: 0}, false},

	{"sint64 - exclusive gt & lt - valid (above)", &cases.SInt64ExLTGT{Val: 11}, true},
	{"sint64 - exclusive gt & lt - valid (below)", &cases.SInt64ExLTGT{Val: -1}, true},
	{"sint64 - exclusive gt & lt - invalid", &cases.SInt64ExLTGT{Val: 5}, false},
	{"sint64 - exclusive gt & lt - invalid (max)", &cases.SInt64ExLTGT{Val: 10}, false},
	{"sint64 - exclusive gt & lt - invalid (min)", &cases.SInt64ExLTGT{Val: 0}, false},

	{"sint64 - gte & lte - valid", &cases.SInt64GTELTE{Val: 200}, true},
	{"sint64 - gte & lte - valid (max)", &cases.SInt64GTELTE{Val: 256}, true},
	{"sint64 - gte & lte - valid (min)", &cases.SInt64GTELTE{Val: 128}, true},
	{"sint64 - gte & lte - invalid (above)", &cases.SInt64GTELTE{Val: 300}, false},
	{"sint64 - gte & lte - invalid (below)", &cases.SInt64GTELTE{Val: 100}, false},

	{"sint64 - exclusive gte & lte - valid (above)", &cases.SInt64ExGTELTE{Val: 300}, true},
	{"sint64 - exclusive gte & lte - valid (below)", &cases.SInt64ExGTELTE{Val: 100}, true},
	{"sint64 - exclusive gte & lte - valid (max)", &cases.SInt64ExGTELTE{Val: 256}, true},
	{"sint64 - exclusive gte & lte - valid (min)", &cases.SInt64ExGTELTE{Val: 128}, true},
	{"sint64 - exclusive gte & lte - invalid", &cases.SInt64ExGTELTE{Val: 200}, false},
}

var fixed32Cases = []TestCase{
	{"fixed32 - none - valid", &cases.Fixed32None{Val: 123}, true},

	{"fixed32 - const - valid", &cases.Fixed32Const{Val: 1}, true},
	{"fixed32 - const - invalid", &cases.Fixed32Const{Val: 2}, false},

	{"fixed32 - in - valid", &cases.Fixed32In{Val: 3}, true},
	{"fixed32 - in - invalid", &cases.Fixed32In{Val: 5}, false},

	{"fixed32 - not in - valid", &cases.Fixed32NotIn{Val: 1}, true},
	{"fixed32 - not in - invalid", &cases.Fixed32NotIn{Val: 0}, false},

	{"fixed32 - lt - valid", &cases.Fixed32LT{Val: 4}, true},
	{"fixed32 - lt - invalid (equal)", &cases.Fixed32LT{Val: 5}, false},
	{"fixed32 - lt - invalid", &cases.Fixed32LT{Val: 6}, false},

	{"fixed32 - lte - valid", &cases.Fixed32LTE{Val: 63}, true},
	{"fixed32 - lte - valid (equal)", &cases.Fixed32LTE{Val: 64}, true},
	{"fixed32 - lte - invalid", &cases.Fixed32LTE{Val: 65}, false},

	{"fixed32 - gt - valid", &cases.Fixed32GT{Val: 17}, true},
	{"fixed32 - gt - invalid (equal)", &cases.Fixed32GT{Val: 16}, false},
	{"fixed32 - gt - invalid", &cases.Fixed32GT{Val: 15}, false},

	{"fixed32 - gte - valid", &cases.Fixed32GTE{Val: 9}, true},
	{"fixed32 - gte - valid (equal)", &cases.Fixed32GTE{Val: 8}, true},
	{"fixed32 - gte - invalid", &cases.Fixed32GTE{Val: 7}, false},

	{"fixed32 - gt & lt - valid", &cases.Fixed32GTLT{Val: 7}, true},
	{"fixed32 - gt & lt - invalid (above)", &cases.Fixed32GTLT{Val: 11}, false},
	{"fixed32 - gt & lt - invalid (below)", &cases.Fixed32GTLT{Val: 1}, false},
	{"fixed32 - gt & lt - invalid (max)", &cases.Fixed32GTLT{Val: 10}, false},
	{"fixed32 - gt & lt - invalid (min)", &cases.Fixed32GTLT{Val: 5}, false},

	{"fixed32 - exclusive gt & lt - valid (above)", &cases.Fixed32ExLTGT{Val: 11}, true},
	{"fixed32 - exclusive gt & lt - valid (below)", &cases.Fixed32ExLTGT{Val: 4}, true},
	{"fixed32 - exclusive gt & lt - invalid", &cases.Fixed32ExLTGT{Val: 7}, false},
	{"fixed32 - exclusive gt & lt - invalid (max)", &cases.Fixed32ExLTGT{Val: 10}, false},
	{"fixed32 - exclusive gt & lt - invalid (min)", &cases.Fixed32ExLTGT{Val: 5}, false},

	{"fixed32 - gte & lte - valid", &cases.Fixed32GTELTE{Val: 200}, true},
	{"fixed32 - gte & lte - valid (max)", &cases.Fixed32GTELTE{Val: 256}, true},
	{"fixed32 - gte & lte - valid (min)", &cases.Fixed32GTELTE{Val: 128}, true},
	{"fixed32 - gte & lte - invalid (above)", &cases.Fixed32GTELTE{Val: 300}, false},
	{"fixed32 - gte & lte - invalid (below)", &cases.Fixed32GTELTE{Val: 100}, false},

	{"fixed32 - exclusive gte & lte - valid (above)", &cases.Fixed32ExGTELTE{Val: 300}, true},
	{"fixed32 - exclusive gte & lte - valid (below)", &cases.Fixed32ExGTELTE{Val: 100}, true},
	{"fixed32 - exclusive gte & lte - valid (max)", &cases.Fixed32ExGTELTE{Val: 256}, true},
	{"fixed32 - exclusive gte & lte - valid (min)", &cases.Fixed32ExGTELTE{Val: 128}, true},
	{"fixed32 - exclusive gte & lte - invalid", &cases.Fixed32ExGTELTE{Val: 200}, false},
}

var fixed64Cases = []TestCase{
	{"fixed64 - none - valid", &cases.Fixed64None{Val: 123}, true},

	{"fixed64 - const - valid", &cases.Fixed64Const{Val: 1}, true},
	{"fixed64 - const - invalid", &cases.Fixed64Const{Val: 2}, false},

	{"fixed64 - in - valid", &cases.Fixed64In{Val: 3}, true},
	{"fixed64 - in - invalid", &cases.Fixed64In{Val: 5}, false},

	{"fixed64 - not in - valid", &cases.Fixed64NotIn{Val: 1}, true},
	{"fixed64 - not in - invalid", &cases.Fixed64NotIn{Val: 0}, false},

	{"fixed64 - lt - valid", &cases.Fixed64LT{Val: 4}, true},
	{"fixed64 - lt - invalid (equal)", &cases.Fixed64LT{Val: 5}, false},
	{"fixed64 - lt - invalid", &cases.Fixed64LT{Val: 6}, false},

	{"fixed64 - lte - valid", &cases.Fixed64LTE{Val: 63}, true},
	{"fixed64 - lte - valid (equal)", &cases.Fixed64LTE{Val: 64}, true},
	{"fixed64 - lte - invalid", &cases.Fixed64LTE{Val: 65}, false},

	{"fixed64 - gt - valid", &cases.Fixed64GT{Val: 17}, true},
	{"fixed64 - gt - invalid (equal)", &cases.Fixed64GT{Val: 16}, false},
	{"fixed64 - gt - invalid", &cases.Fixed64GT{Val: 15}, false},

	{"fixed64 - gte - valid", &cases.Fixed64GTE{Val: 9}, true},
	{"fixed64 - gte - valid (equal)", &cases.Fixed64GTE{Val: 8}, true},
	{"fixed64 - gte - invalid", &cases.Fixed64GTE{Val: 7}, false},

	{"fixed64 - gt & lt - valid", &cases.Fixed64GTLT{Val: 7}, true},
	{"fixed64 - gt & lt - invalid (above)", &cases.Fixed64GTLT{Val: 11}, false},
	{"fixed64 - gt & lt - invalid (below)", &cases.Fixed64GTLT{Val: 1}, false},
	{"fixed64 - gt & lt - invalid (max)", &cases.Fixed64GTLT{Val: 10}, false},
	{"fixed64 - gt & lt - invalid (min)", &cases.Fixed64GTLT{Val: 5}, false},

	{"fixed64 - exclusive gt & lt - valid (above)", &cases.Fixed64ExLTGT{Val: 11}, true},
	{"fixed64 - exclusive gt & lt - valid (below)", &cases.Fixed64ExLTGT{Val: 4}, true},
	{"fixed64 - exclusive gt & lt - invalid", &cases.Fixed64ExLTGT{Val: 7}, false},
	{"fixed64 - exclusive gt & lt - invalid (max)", &cases.Fixed64ExLTGT{Val: 10}, false},
	{"fixed64 - exclusive gt & lt - invalid (min)", &cases.Fixed64ExLTGT{Val: 5}, false},

	{"fixed64 - gte & lte - valid", &cases.Fixed64GTELTE{Val: 200}, true},
	{"fixed64 - gte & lte - valid (max)", &cases.Fixed64GTELTE{Val: 256}, true},
	{"fixed64 - gte & lte - valid (min)", &cases.Fixed64GTELTE{Val: 128}, true},
	{"fixed64 - gte & lte - invalid (above)", &cases.Fixed64GTELTE{Val: 300}, false},
	{"fixed64 - gte & lte - invalid (below)", &cases.Fixed64GTELTE{Val: 100}, false},

	{"fixed64 - exclusive gte & lte - valid (above)", &cases.Fixed64ExGTELTE{Val: 300}, true},
	{"fixed64 - exclusive gte & lte - valid (below)", &cases.Fixed64ExGTELTE{Val: 100}, true},
	{"fixed64 - exclusive gte & lte - valid (max)", &cases.Fixed64ExGTELTE{Val: 256}, true},
	{"fixed64 - exclusive gte & lte - valid (min)", &cases.Fixed64ExGTELTE{Val: 128}, true},
	{"fixed64 - exclusive gte & lte - invalid", &cases.Fixed64ExGTELTE{Val: 200}, false},
}

var sfixed32Cases = []TestCase{
	{"sfixed32 - none - valid", &cases.SFixed32None{Val: 123}, true},

	{"sfixed32 - const - valid", &cases.SFixed32Const{Val: 1}, true},
	{"sfixed32 - const - invalid", &cases.SFixed32Const{Val: 2}, false},

	{"sfixed32 - in - valid", &cases.SFixed32In{Val: 3}, true},
	{"sfixed32 - in - invalid", &cases.SFixed32In{Val: 5}, false},

	{"sfixed32 - not in - valid", &cases.SFixed32NotIn{Val: 1}, true},
	{"sfixed32 - not in - invalid", &cases.SFixed32NotIn{Val: 0}, false},

	{"sfixed32 - lt - valid", &cases.SFixed32LT{Val: -1}, true},
	{"sfixed32 - lt - invalid (equal)", &cases.SFixed32LT{Val: 0}, false},
	{"sfixed32 - lt - invalid", &cases.SFixed32LT{Val: 1}, false},

	{"sfixed32 - lte - valid", &cases.SFixed32LTE{Val: 63}, true},
	{"sfixed32 - lte - valid (equal)", &cases.SFixed32LTE{Val: 64}, true},
	{"sfixed32 - lte - invalid", &cases.SFixed32LTE{Val: 65}, false},

	{"sfixed32 - gt - valid", &cases.SFixed32GT{Val: 17}, true},
	{"sfixed32 - gt - invalid (equal)", &cases.SFixed32GT{Val: 16}, false},
	{"sfixed32 - gt - invalid", &cases.SFixed32GT{Val: 15}, false},

	{"sfixed32 - gte - valid", &cases.SFixed32GTE{Val: 9}, true},
	{"sfixed32 - gte - valid (equal)", &cases.SFixed32GTE{Val: 8}, true},
	{"sfixed32 - gte - invalid", &cases.SFixed32GTE{Val: 7}, false},

	{"sfixed32 - gt & lt - valid", &cases.SFixed32GTLT{Val: 5}, true},
	{"sfixed32 - gt & lt - invalid (above)", &cases.SFixed32GTLT{Val: 11}, false},
	{"sfixed32 - gt & lt - invalid (below)", &cases.SFixed32GTLT{Val: -1}, false},
	{"sfixed32 - gt & lt - invalid (max)", &cases.SFixed32GTLT{Val: 10}, false},
	{"sfixed32 - gt & lt - invalid (min)", &cases.SFixed32GTLT{Val: 0}, false},

	{"sfixed32 - exclusive gt & lt - valid (above)", &cases.SFixed32ExLTGT{Val: 11}, true},
	{"sfixed32 - exclusive gt & lt - valid (below)", &cases.SFixed32ExLTGT{Val: -1}, true},
	{"sfixed32 - exclusive gt & lt - invalid", &cases.SFixed32ExLTGT{Val: 5}, false},
	{"sfixed32 - exclusive gt & lt - invalid (max)", &cases.SFixed32ExLTGT{Val: 10}, false},
	{"sfixed32 - exclusive gt & lt - invalid (min)", &cases.SFixed32ExLTGT{Val: 0}, false},

	{"sfixed32 - gte & lte - valid", &cases.SFixed32GTELTE{Val: 200}, true},
	{"sfixed32 - gte & lte - valid (max)", &cases.SFixed32GTELTE{Val: 256}, true},
	{"sfixed32 - gte & lte - valid (min)", &cases.SFixed32GTELTE{Val: 128}, true},
	{"sfixed32 - gte & lte - invalid (above)", &cases.SFixed32GTELTE{Val: 300}, false},
	{"sfixed32 - gte & lte - invalid (below)", &cases.SFixed32GTELTE{Val: 100}, false},

	{"sfixed32 - exclusive gte & lte - valid (above)", &cases.SFixed32ExGTELTE{Val: 300}, true},
	{"sfixed32 - exclusive gte & lte - valid (below)", &cases.SFixed32ExGTELTE{Val: 100}, true},
	{"sfixed32 - exclusive gte & lte - valid (max)", &cases.SFixed32ExGTELTE{Val: 256}, true},
	{"sfixed32 - exclusive gte & lte - valid (min)", &cases.SFixed32ExGTELTE{Val: 128}, true},
	{"sfixed32 - exclusive gte & lte - invalid", &cases.SFixed32ExGTELTE{Val: 200}, false},
}

var sfixed64Cases = []TestCase{
	{"sfixed64 - none - valid", &cases.SFixed64None{Val: 123}, true},

	{"sfixed64 - const - valid", &cases.SFixed64Const{Val: 1}, true},
	{"sfixed64 - const - invalid", &cases.SFixed64Const{Val: 2}, false},

	{"sfixed64 - in - valid", &cases.SFixed64In{Val: 3}, true},
	{"sfixed64 - in - invalid", &cases.SFixed64In{Val: 5}, false},

	{"sfixed64 - not in - valid", &cases.SFixed64NotIn{Val: 1}, true},
	{"sfixed64 - not in - invalid", &cases.SFixed64NotIn{Val: 0}, false},

	{"sfixed64 - lt - valid", &cases.SFixed64LT{Val: -1}, true},
	{"sfixed64 - lt - invalid (equal)", &cases.SFixed64LT{Val: 0}, false},
	{"sfixed64 - lt - invalid", &cases.SFixed64LT{Val: 1}, false},

	{"sfixed64 - lte - valid", &cases.SFixed64LTE{Val: 63}, true},
	{"sfixed64 - lte - valid (equal)", &cases.SFixed64LTE{Val: 64}, true},
	{"sfixed64 - lte - invalid", &cases.SFixed64LTE{Val: 65}, false},

	{"sfixed64 - gt - valid", &cases.SFixed64GT{Val: 17}, true},
	{"sfixed64 - gt - invalid (equal)", &cases.SFixed64GT{Val: 16}, false},
	{"sfixed64 - gt - invalid", &cases.SFixed64GT{Val: 15}, false},

	{"sfixed64 - gte - valid", &cases.SFixed64GTE{Val: 9}, true},
	{"sfixed64 - gte - valid (equal)", &cases.SFixed64GTE{Val: 8}, true},
	{"sfixed64 - gte - invalid", &cases.SFixed64GTE{Val: 7}, false},

	{"sfixed64 - gt & lt - valid", &cases.SFixed64GTLT{Val: 5}, true},
	{"sfixed64 - gt & lt - invalid (above)", &cases.SFixed64GTLT{Val: 11}, false},
	{"sfixed64 - gt & lt - invalid (below)", &cases.SFixed64GTLT{Val: -1}, false},
	{"sfixed64 - gt & lt - invalid (max)", &cases.SFixed64GTLT{Val: 10}, false},
	{"sfixed64 - gt & lt - invalid (min)", &cases.SFixed64GTLT{Val: 0}, false},

	{"sfixed64 - exclusive gt & lt - valid (above)", &cases.SFixed64ExLTGT{Val: 11}, true},
	{"sfixed64 - exclusive gt & lt - valid (below)", &cases.SFixed64ExLTGT{Val: -1}, true},
	{"sfixed64 - exclusive gt & lt - invalid", &cases.SFixed64ExLTGT{Val: 5}, false},
	{"sfixed64 - exclusive gt & lt - invalid (max)", &cases.SFixed64ExLTGT{Val: 10}, false},
	{"sfixed64 - exclusive gt & lt - invalid (min)", &cases.SFixed64ExLTGT{Val: 0}, false},

	{"sfixed64 - gte & lte - valid", &cases.SFixed64GTELTE{Val: 200}, true},
	{"sfixed64 - gte & lte - valid (max)", &cases.SFixed64GTELTE{Val: 256}, true},
	{"sfixed64 - gte & lte - valid (min)", &cases.SFixed64GTELTE{Val: 128}, true},
	{"sfixed64 - gte & lte - invalid (above)", &cases.SFixed64GTELTE{Val: 300}, false},
	{"sfixed64 - gte & lte - invalid (below)", &cases.SFixed64GTELTE{Val: 100}, false},

	{"sfixed64 - exclusive gte & lte - valid (above)", &cases.SFixed64ExGTELTE{Val: 300}, true},
	{"sfixed64 - exclusive gte & lte - valid (below)", &cases.SFixed64ExGTELTE{Val: 100}, true},
	{"sfixed64 - exclusive gte & lte - valid (max)", &cases.SFixed64ExGTELTE{Val: 256}, true},
	{"sfixed64 - exclusive gte & lte - valid (min)", &cases.SFixed64ExGTELTE{Val: 128}, true},
	{"sfixed64 - exclusive gte & lte - invalid", &cases.SFixed64ExGTELTE{Val: 200}, false},
}

var boolCases = []TestCase{
	{"bool - none - valid", &cases.BoolNone{Val: true}, true},
	{"bool - const (true) - valid", &cases.BoolConstTrue{Val: true}, true},
	{"bool - const (true) - invalid", &cases.BoolConstTrue{Val: false}, false},
	{"bool - const (false) - valid", &cases.BoolConstFalse{Val: false}, true},
	{"bool - const (false) - invalid", &cases.BoolConstFalse{Val: true}, false},
}

var stringCases = []TestCase{
	{"string - none - valid", &cases.StringNone{Val: "quux"}, true},

	{"string - const - valid", &cases.StringConst{Val: "foo"}, true},
	{"string - const - invalid", &cases.StringConst{Val: "bar"}, false},

	{"string - in - valid", &cases.StringIn{Val: "bar"}, true},
	{"string - in - invalid", &cases.StringIn{Val: "quux"}, false},
	{"string - not in - valid", &cases.StringNotIn{Val: "quux"}, true},
	{"string - not in - invalid", &cases.StringNotIn{Val: "fizz"}, false},

	{"string - len - valid", &cases.StringLen{Val: "baz"}, true},
	{"string - len - valid (multibyte)", &cases.StringLen{Val: "你好吖"}, true},
	{"string - len - invalid (lt)", &cases.StringLen{Val: "go"}, false},
	{"string - len - invalid (gt)", &cases.StringLen{Val: "fizz"}, false},
	{"string - len - invalid (multibyte)", &cases.StringLen{Val: "你好"}, false},

	{"string - min len - valid", &cases.StringMinLen{Val: "protoc"}, true},
	{"string - min len - valid (min)", &cases.StringMinLen{Val: "baz"}, true},
	{"string - min len - invalid", &cases.StringMinLen{Val: "go"}, false},
	{"string - min len - invalid (multibyte)", &cases.StringMinLen{Val: "你好"}, false},

	{"string - max len - valid", &cases.StringMaxLen{Val: "foo"}, true},
	{"string - max len - valid (max)", &cases.StringMaxLen{Val: "proto"}, true},
	{"string - max len - valid (multibyte)", &cases.StringMaxLen{Val: "你好你好"}, true},
	{"string - max len - invalid", &cases.StringMaxLen{Val: "1234567890"}, false},

	{"string - min/max len - valid", &cases.StringMinMaxLen{Val: "quux"}, true},
	{"string - min/max len - valid (min)", &cases.StringMinMaxLen{Val: "foo"}, true},
	{"string - min/max len - valid (max)", &cases.StringMinMaxLen{Val: "proto"}, true},
	{"string - min/max len - valid (multibyte)", &cases.StringMinMaxLen{Val: "你好你好"}, true},
	{"string - min/max len - invalid (below)", &cases.StringMinMaxLen{Val: "go"}, false},
	{"string - min/max len - invalid (above)", &cases.StringMinMaxLen{Val: "validate"}, false},

	{"string - equal min/max len - valid", &cases.StringEqualMinMaxLen{Val: "proto"}, true},
	{"string - equal min/max len - invalid", &cases.StringEqualMinMaxLen{Val: "validate"}, false},

	{"string - len bytes - valid", &cases.StringLenBytes{Val: "pace"}, true},
	{"string - len bytes - invalid (lt)", &cases.StringLenBytes{Val: "val"}, false},
	{"string - len bytes - invalid (gt)", &cases.StringLenBytes{Val: "world"}, false},
	{"string - len bytes - invalid (multibyte)", &cases.StringLenBytes{Val: "世界和平"}, false},

	{"string - min bytes - valid", &cases.StringMinBytes{Val: "proto"}, true},
	{"string - min bytes - valid (min)", &cases.StringMinBytes{Val: "quux"}, true},
	{"string - min bytes - valid (multibyte)", &cases.StringMinBytes{Val: "你好"}, true},
	{"string - min bytes - invalid", &cases.StringMinBytes{Val: ""}, false},

	{"string - max bytes - valid", &cases.StringMaxBytes{Val: "foo"}, true},
	{"string - max bytes - valid (max)", &cases.StringMaxBytes{Val: "12345678"}, true},
	{"string - max bytes - invalid", &cases.StringMaxBytes{Val: "123456789"}, false},
	{"string - max bytes - invalid (multibyte)", &cases.StringMaxBytes{Val: "你好你好你好"}, false},

	{"string - min/max bytes - valid", &cases.StringMinMaxBytes{Val: "protoc"}, true},
	{"string - min/max bytes - valid (min)", &cases.StringMinMaxBytes{Val: "quux"}, true},
	{"string - min/max bytes - valid (max)", &cases.StringMinMaxBytes{Val: "fizzbuzz"}, true},
	{"string - min/max bytes - valid (multibyte)", &cases.StringMinMaxBytes{Val: "你好"}, true},
	{"string - min/max bytes - invalid (below)", &cases.StringMinMaxBytes{Val: "foo"}, false},
	{"string - min/max bytes - invalid (above)", &cases.StringMinMaxBytes{Val: "你好你好你"}, false},

	{"string - equal min/max bytes - valid", &cases.StringEqualMinMaxBytes{Val: "protoc"}, true},
	{"string - equal min/max bytes - invalid", &cases.StringEqualMinMaxBytes{Val: "foo"}, false},

	{"string - pattern - valid", &cases.StringPattern{Val: "Foo123"}, true},
	{"string - pattern - invalid", &cases.StringPattern{Val: "!@#$%^&*()"}, false},
	{"string - pattern - invalid (empty)", &cases.StringPattern{Val: ""}, false},
	{"string - pattern - invalid (null)", &cases.StringPattern{Val: "a\000"}, false},

	{"string - pattern (escapes) - valid", &cases.StringPatternEscapes{Val: "* \\ x"}, true},
	{"string - pattern (escapes) - invalid", &cases.StringPatternEscapes{Val: "invalid"}, false},
	{"string - pattern (escapes) - invalid (empty)", &cases.StringPatternEscapes{Val: ""}, false},

	{"string - prefix - valid", &cases.StringPrefix{Val: "foobar"}, true},
	{"string - prefix - valid (only)", &cases.StringPrefix{Val: "foo"}, true},
	{"string - prefix - invalid", &cases.StringPrefix{Val: "bar"}, false},
	{"string - prefix - invalid (case-sensitive)", &cases.StringPrefix{Val: "Foobar"}, false},

	{"string - contains - valid", &cases.StringContains{Val: "candy bars"}, true},
	{"string - contains - valid (only)", &cases.StringContains{Val: "bar"}, true},
	{"string - contains - invalid", &cases.StringContains{Val: "candy bazs"}, false},
	{"string - contains - invalid (case-sensitive)", &cases.StringContains{Val: "Candy Bars"}, false},

	{"string - not contains - valid", &cases.StringNotContains{Val: "candy bazs"}, true},
	{"string - not contains - valid (case-sensitive)", &cases.StringNotContains{Val: "Candy Bars"}, true},
	{"string - not contains - invalid", &cases.StringNotContains{Val: "candy bars"}, false},
	{"string - not contains - invalid (equal)", &cases.StringNotContains{Val: "bar"}, false},

	{"string - suffix - valid", &cases.StringSuffix{Val: "foobaz"}, true},
	{"string - suffix - valid (only)", &cases.StringSuffix{Val: "baz"}, true},
	{"string - suffix - invalid", &cases.StringSuffix{Val: "foobar"}, false},
	{"string - suffix - invalid (case-sensitive)", &cases.StringSuffix{Val: "FooBaz"}, false},

	{"string - email - valid", &cases.StringEmail{Val: "foo@bar.com"}, true},
	{"string - email - valid (name)", &cases.StringEmail{Val: "John Smith <foo@bar.com>"}, true},
	{"string - email - invalid", &cases.StringEmail{Val: "foobar"}, false},
	{"string - email - invalid (local segment too long)", &cases.StringEmail{Val: "x0123456789012345678901234567890123456789012345678901234567890123456789@example.com"}, false},
	{"string - email - invalid (hostname too long)", &cases.StringEmail{Val: "foo@x0123456789012345678901234567890123456789012345678901234567890123456789.com"}, false},
	{"string - email - invalid (bad hostname)", &cases.StringEmail{Val: "foo@-bar.com"}, false},
	{"string - email - empty", &cases.StringEmail{Val: ""}, false},

	{"string - address - valid hostname", &cases.StringAddress{Val: "example.com"}, true},
	{"string - address - valid hostname (uppercase)", &cases.StringAddress{Val: "ASD.example.com"}, true},
	{"string - address - valid hostname (hyphens)", &cases.StringAddress{Val: "foo-bar.com"}, true},
	{"string - address - valid hostname (trailing dot)", &cases.StringAddress{Val: "example.com."}, true},
	{"string - address - invalid hostname", &cases.StringAddress{Val: "!@#$%^&"}, false},
	{"string - address - invalid hostname (underscore)", &cases.StringAddress{Val: "foo_bar.com"}, false},
	{"string - address - invalid hostname (too long)", &cases.StringAddress{Val: "x0123456789012345678901234567890123456789012345678901234567890123456789.com"}, false},
	{"string - address - invalid hostname (trailing hyphens)", &cases.StringAddress{Val: "foo-bar-.com"}, false},
	{"string - address - invalid hostname (leading hyphens)", &cases.StringAddress{Val: "foo-bar.-com"}, false},
	{"string - address - invalid hostname (empty)", &cases.StringAddress{Val: "asd..asd.com"}, false},
	{"string - address - invalid hostname (IDNs)", &cases.StringAddress{Val: "你好.com"}, false},
	{"string - address - valid ip (v4)", &cases.StringAddress{Val: "192.168.0.1"}, true},
	{"string - address - valid ip (v6)", &cases.StringAddress{Val: "3e::99"}, true},
	{"string - address - invalid ip", &cases.StringAddress{Val: "ff::fff::0b"}, false},

	{"string - hostname - valid", &cases.StringHostname{Val: "example.com"}, true},
	{"string - hostname - valid (uppercase)", &cases.StringHostname{Val: "ASD.example.com"}, true},
	{"string - hostname - valid (hyphens)", &cases.StringHostname{Val: "foo-bar.com"}, true},
	{"string - hostname - valid (trailing dot)", &cases.StringHostname{Val: "example.com."}, true},
	{"string - hostname - invalid", &cases.StringHostname{Val: "!@#$%^&"}, false},
	{"string - hostname - invalid (underscore)", &cases.StringHostname{Val: "foo_bar.com"}, false},
	{"string - hostname - invalid (too long)", &cases.StringHostname{Val: "x0123456789012345678901234567890123456789012345678901234567890123456789.com"}, false},
	{"string - hostname - invalid (trailing hyphens)", &cases.StringHostname{Val: "foo-bar-.com"}, false},
	{"string - hostname - invalid (leading hyphens)", &cases.StringHostname{Val: "foo-bar.-com"}, false},
	{"string - hostname - invalid (empty)", &cases.StringHostname{Val: "asd..asd.com"}, false},
	{"string - hostname - invalid (IDNs)", &cases.StringHostname{Val: "你好.com"}, false},

	{"string - IP - valid (v4)", &cases.StringIP{Val: "192.168.0.1"}, true},
	{"string - IP - valid (v6)", &cases.StringIP{Val: "3e::99"}, true},
	{"string - IP - invalid", &cases.StringIP{Val: "foobar"}, false},

	{"string - IPv4 - valid", &cases.StringIPv4{Val: "192.168.0.1"}, true},
	{"string - IPv4 - invalid", &cases.StringIPv4{Val: "foobar"}, false},
	{"string - IPv4 - invalid (erroneous)", &cases.StringIPv4{Val: "256.0.0.0"}, false},
	{"string - IPv4 - invalid (v6)", &cases.StringIPv4{Val: "3e::99"}, false},

	{"string - IPv6 - valid", &cases.StringIPv6{Val: "2001:0db8:85a3:0000:0000:8a2e:0370:7334"}, true},
	{"string - IPv6 - valid (collapsed)", &cases.StringIPv6{Val: "2001:db8:85a3::8a2e:370:7334"}, true},
	{"string - IPv6 - invalid", &cases.StringIPv6{Val: "foobar"}, false},
	{"string - IPv6 - invalid (v4)", &cases.StringIPv6{Val: "192.168.0.1"}, false},
	{"string - IPv6 - invalid (erroneous)", &cases.StringIPv6{Val: "ff::fff::0b"}, false},

	{"string - URI - valid", &cases.StringURI{Val: "http://example.com/foo/bar?baz=quux"}, true},
	{"string - URI - invalid", &cases.StringURI{Val: "!@#$%^&*%$#"}, false},
	{"string - URI - invalid (relative)", &cases.StringURI{Val: "/foo/bar?baz=quux"}, false},

	{"string - URI - valid", &cases.StringURIRef{Val: "http://example.com/foo/bar?baz=quux"}, true},
	{"string - URI - valid (relative)", &cases.StringURIRef{Val: "/foo/bar?baz=quux"}, true},
	{"string - URI - invalid", &cases.StringURIRef{Val: "!@#$%^&*%$#"}, false},

	{"string - UUID - valid (nil)", &cases.StringUUID{Val: "00000000-0000-0000-0000-000000000000"}, true},
	{"string - UUID - valid (v1)", &cases.StringUUID{Val: "b45c0c80-8880-11e9-a5b1-000000000000"}, true},
	{"string - UUID - valid (v1 - case-insensitive)", &cases.StringUUID{Val: "B45C0C80-8880-11E9-A5B1-000000000000"}, true},
	{"string - UUID - valid (v2)", &cases.StringUUID{Val: "b45c0c80-8880-21e9-a5b1-000000000000"}, true},
	{"string - UUID - valid (v2 - case-insensitive)", &cases.StringUUID{Val: "B45C0C80-8880-21E9-A5B1-000000000000"}, true},
	{"string - UUID - valid (v3)", &cases.StringUUID{Val: "a3bb189e-8bf9-3888-9912-ace4e6543002"}, true},
	{"string - UUID - valid (v3 - case-insensitive)", &cases.StringUUID{Val: "A3BB189E-8BF9-3888-9912-ACE4E6543002"}, true},
	{"string - UUID - valid (v4)", &cases.StringUUID{Val: "8b208305-00e8-4460-a440-5e0dcd83bb0a"}, true},
	{"string - UUID - valid (v4 - case-insensitive)", &cases.StringUUID{Val: "8B208305-00E8-4460-A440-5E0DCD83BB0A"}, true},
	{"string - UUID - valid (v5)", &cases.StringUUID{Val: "a6edc906-2f9f-5fb2-a373-efac406f0ef2"}, true},
	{"string - UUID - valid (v5 - case-insensitive)", &cases.StringUUID{Val: "A6EDC906-2F9F-5FB2-A373-EFAC406F0EF2"}, true},
	{"string - UUID - invalid", &cases.StringUUID{Val: "foobar"}, false},
	{"string - UUID - invalid (bad UUID)", &cases.StringUUID{Val: "ffffffff-ffff-ffff-ffff-fffffffffffff"}, false},

	{"string - http header name - valid", &cases.StringHttpHeaderName{Val: "clustername"}, true},
	{"string - http header name - valid", &cases.StringHttpHeaderName{Val: ":path"}, true},
	{"string - http header name - valid (nums)", &cases.StringHttpHeaderName{Val: "cluster-123"}, true},
	{"string - http header name - valid (special token)", &cases.StringHttpHeaderName{Val: "!+#&.%"}, true},
	{"string - http header name - valid (period)", &cases.StringHttpHeaderName{Val: "CLUSTER.NAME"}, true},
	{"string - http header name - invalid", &cases.StringHttpHeaderName{Val: ":"}, false},
	{"string - http header name - invalid", &cases.StringHttpHeaderName{Val: ":path:"}, false},
	{"string - http header name - invalid (space)", &cases.StringHttpHeaderName{Val: "cluster name"}, false},
	{"string - http header name - invalid (return)", &cases.StringHttpHeaderName{Val: "example\r"}, false},
	{"string - http header name - invalid (tab)", &cases.StringHttpHeaderName{Val: "example\t"}, false},
	{"string - http header name - invalid (slash)", &cases.StringHttpHeaderName{Val: "/test/long/url"}, false},

	{"string - http header value - valid", &cases.StringHttpHeaderValue{Val: "cluster.name.123"}, true},
	{"string - http header value - valid (uppercase)", &cases.StringHttpHeaderValue{Val: "/TEST/LONG/URL"}, true},
	{"string - http header value - valid (spaces)", &cases.StringHttpHeaderValue{Val: "cluster name"}, true},
	{"string - http header value - valid (tab)", &cases.StringHttpHeaderValue{Val: "example\t"}, true},
	{"string - http header value - valid (special token)", &cases.StringHttpHeaderValue{Val: "!#%&./+"}, true},
	{"string - http header value - invalid (NUL)", &cases.StringHttpHeaderValue{Val: "foo\u0000bar"}, false},
	{"string - http header value - invalid (DEL)", &cases.StringHttpHeaderValue{Val: "\u007f"}, false},
	{"string - http header value - invalid", &cases.StringHttpHeaderValue{Val: "example\r"}, false},

	{"string - non-strict valid header - valid", &cases.StringValidHeader{Val: "cluster.name.123"}, true},
	{"string - non-strict valid header - valid (uppercase)", &cases.StringValidHeader{Val: "/TEST/LONG/URL"}, true},
	{"string - non-strict valid header - valid (spaces)", &cases.StringValidHeader{Val: "cluster name"}, true},
	{"string - non-strict valid header - valid (tab)", &cases.StringValidHeader{Val: "example\t"}, true},
	{"string - non-strict valid header - valid (DEL)", &cases.StringValidHeader{Val: "\u007f"}, true},
	{"string - non-strict valid header - invalid (NUL)", &cases.StringValidHeader{Val: "foo\u0000bar"}, false},
	{"string - non-strict valid header - invalid (CR)", &cases.StringValidHeader{Val: "example\r"}, false},
	{"string - non-strict valid header - invalid (NL)", &cases.StringValidHeader{Val: "exa\u000Ample"}, false},
}

var bytesCases = []TestCase{
	{"bytes - none - valid", &cases.BytesNone{Val: []byte("quux")}, true},

	{"bytes - const - valid", &cases.BytesConst{Val: []byte("foo")}, true},
	{"bytes - const - invalid", &cases.BytesConst{Val: []byte("bar")}, false},

	{"bytes - in - valid", &cases.BytesIn{Val: []byte("bar")}, true},
	{"bytes - in - invalid", &cases.BytesIn{Val: []byte("quux")}, false},
	{"bytes - not in - valid", &cases.BytesNotIn{Val: []byte("quux")}, true},
	{"bytes - not in - invalid", &cases.BytesNotIn{Val: []byte("fizz")}, false},

	{"bytes - len - valid", &cases.BytesLen{Val: []byte("baz")}, true},
	{"bytes - len - invalid (lt)", &cases.BytesLen{Val: []byte("go")}, false},
	{"bytes - len - invalid (gt)", &cases.BytesLen{Val: []byte("fizz")}, false},

	{"bytes - min len - valid", &cases.BytesMinLen{Val: []byte("fizz")}, true},
	{"bytes - min len - valid (min)", &cases.BytesMinLen{Val: []byte("baz")}, true},
	{"bytes - min len - invalid", &cases.BytesMinLen{Val: []byte("go")}, false},

	{"bytes - max len - valid", &cases.BytesMaxLen{Val: []byte("foo")}, true},
	{"bytes - max len - valid (max)", &cases.BytesMaxLen{Val: []byte("proto")}, true},
	{"bytes - max len - invalid", &cases.BytesMaxLen{Val: []byte("1234567890")}, false},

	{"bytes - min/max len - valid", &cases.BytesMinMaxLen{Val: []byte("quux")}, true},
	{"bytes - min/max len - valid (min)", &cases.BytesMinMaxLen{Val: []byte("foo")}, true},
	{"bytes - min/max len - valid (max)", &cases.BytesMinMaxLen{Val: []byte("proto")}, true},
	{"bytes - min/max len - invalid (below)", &cases.BytesMinMaxLen{Val: []byte("go")}, false},
	{"bytes - min/max len - invalid (above)", &cases.BytesMinMaxLen{Val: []byte("validate")}, false},

	{"bytes - equal min/max len - valid", &cases.BytesEqualMinMaxLen{Val: []byte("proto")}, true},
	{"bytes - equal min/max len - invalid", &cases.BytesEqualMinMaxLen{Val: []byte("validate")}, false},

	{"bytes - pattern - valid", &cases.BytesPattern{Val: []byte("Foo123")}, true},
	{"bytes - pattern - invalid", &cases.BytesPattern{Val: []byte("你好你好")}, false},
	{"bytes - pattern - invalid (empty)", &cases.BytesPattern{Val: []byte("")}, false},

	{"bytes - prefix - valid", &cases.BytesPrefix{Val: []byte{0x99, 0x9f, 0x08}}, true},
	{"bytes - prefix - valid (only)", &cases.BytesPrefix{Val: []byte{0x99}}, true},
	{"bytes - prefix - invalid", &cases.BytesPrefix{Val: []byte("bar")}, false},

	{"bytes - contains - valid", &cases.BytesContains{Val: []byte("candy bars")}, true},
	{"bytes - contains - valid (only)", &cases.BytesContains{Val: []byte("bar")}, true},
	{"bytes - contains - invalid", &cases.BytesContains{Val: []byte("candy bazs")}, false},

	{"bytes - suffix - valid", &cases.BytesSuffix{Val: []byte{0x62, 0x75, 0x7A, 0x7A}}, true},
	{"bytes - suffix - valid (only)", &cases.BytesSuffix{Val: []byte("\x62\x75\x7A\x7A")}, true},
	{"bytes - suffix - invalid", &cases.BytesSuffix{Val: []byte("foobar")}, false},
	{"bytes - suffix - invalid (case-sensitive)", &cases.BytesSuffix{Val: []byte("FooBaz")}, false},

	{"bytes - IP - valid (v4)", &cases.BytesIP{Val: []byte{0xC0, 0xA8, 0x00, 0x01}}, true},
	{"bytes - IP - valid (v6)", &cases.BytesIP{Val: []byte("\x20\x01\x0D\xB8\x85\xA3\x00\x00\x00\x00\x8A\x2E\x03\x70\x73\x34")}, true},
	{"bytes - IP - invalid", &cases.BytesIP{Val: []byte("foobar")}, false},

	{"bytes - IPv4 - valid", &cases.BytesIPv4{Val: []byte{0xC0, 0xA8, 0x00, 0x01}}, true},
	{"bytes - IPv4 - invalid", &cases.BytesIPv4{Val: []byte("foobar")}, false},
	{"bytes - IPv4 - invalid (v6)", &cases.BytesIPv4{Val: []byte("\x20\x01\x0D\xB8\x85\xA3\x00\x00\x00\x00\x8A\x2E\x03\x70\x73\x34")}, false},

	{"bytes - IPv6 - valid", &cases.BytesIPv6{Val: []byte("\x20\x01\x0D\xB8\x85\xA3\x00\x00\x00\x00\x8A\x2E\x03\x70\x73\x34")}, true},
	{"bytes - IPv6 - invalid", &cases.BytesIPv6{Val: []byte("fooar")}, false},
	{"bytes - IPv6 - invalid (v4)", &cases.BytesIPv6{Val: []byte{0xC0, 0xA8, 0x00, 0x01}}, false},
}

var enumCases = []TestCase{
	{"enum - none - valid", &cases.EnumNone{Val: cases.TestEnum_ONE}, true},

	{"enum - const - valid", &cases.EnumConst{Val: cases.TestEnum_TWO}, true},
	{"enum - const - invalid", &cases.EnumConst{Val: cases.TestEnum_ONE}, false},
	{"enum alias - const - valid", &cases.EnumAliasConst{Val: cases.TestEnumAlias_C}, true},
	{"enum alias - const - valid (alias)", &cases.EnumAliasConst{Val: cases.TestEnumAlias_GAMMA}, true},
	{"enum alias - const - invalid", &cases.EnumAliasConst{Val: cases.TestEnumAlias_ALPHA}, false},

	{"enum - defined_only - valid", &cases.EnumDefined{Val: 0}, true},
	{"enum - defined_only - invalid", &cases.EnumDefined{Val: math.MaxInt32}, false},
	{"enum alias - defined_only - valid", &cases.EnumAliasDefined{Val: 1}, true},
	{"enum alias - defined_only - invalid", &cases.EnumAliasDefined{Val: math.MaxInt32}, false},

	{"enum - in - valid", &cases.EnumIn{Val: cases.TestEnum_TWO}, true},
	{"enum - in - invalid", &cases.EnumIn{Val: cases.TestEnum_ONE}, false},
	{"enum alias - in - valid", &cases.EnumAliasIn{Val: cases.TestEnumAlias_A}, true},
	{"enum alias - in - valid (alias)", &cases.EnumAliasIn{Val: cases.TestEnumAlias_ALPHA}, true},
	{"enum alias - in - invalid", &cases.EnumAliasIn{Val: cases.TestEnumAlias_BETA}, false},

	{"enum - not in - valid", &cases.EnumNotIn{Val: cases.TestEnum_ZERO}, true},
	{"enum - not in - valid (undefined)", &cases.EnumNotIn{Val: math.MaxInt32}, true},
	{"enum - not in - invalid", &cases.EnumNotIn{Val: cases.TestEnum_ONE}, false},
	{"enum alias - not in - valid", &cases.EnumAliasNotIn{Val: cases.TestEnumAlias_ALPHA}, true},
	{"enum alias - not in - invalid", &cases.EnumAliasNotIn{Val: cases.TestEnumAlias_B}, false},
	{"enum alias - not in - invalid (alias)", &cases.EnumAliasNotIn{Val: cases.TestEnumAlias_BETA}, false},

	{"enum external - defined_only - valid", &cases.EnumExternal{Val: other_package.Embed_VALUE}, true},
	{"enum external - defined_only - invalid", &cases.EnumExternal{Val: math.MaxInt32}, false},

	{"enum repeated - defined_only - valid", &cases.RepeatedEnumDefined{Val: []cases.TestEnum{cases.TestEnum_ONE, cases.TestEnum_TWO}}, true},
	{"enum repeated - defined_only - invalid", &cases.RepeatedEnumDefined{Val: []cases.TestEnum{cases.TestEnum_ONE, math.MaxInt32}}, false},

	{"enum repeated (external) - defined_only - valid", &cases.RepeatedExternalEnumDefined{Val: []other_package.Embed_Enumerated{other_package.Embed_VALUE}}, true},
	{"enum repeated (external) - defined_only - invalid", &cases.RepeatedExternalEnumDefined{Val: []other_package.Embed_Enumerated{math.MaxInt32}}, false},

	{"enum map - defined_only - valid", &cases.MapEnumDefined{Val: map[string]cases.TestEnum{"foo": cases.TestEnum_TWO}}, true},
	{"enum map - defined_only - invalid", &cases.MapEnumDefined{Val: map[string]cases.TestEnum{"foo": math.MaxInt32}}, false},

	{"enum map (external) - defined_only - valid", &cases.MapExternalEnumDefined{Val: map[string]other_package.Embed_Enumerated{"foo": other_package.Embed_VALUE}}, true},
	{"enum map (external) - defined_only - invalid", &cases.MapExternalEnumDefined{Val: map[string]other_package.Embed_Enumerated{"foo": math.MaxInt32}}, false},
}

var messageCases = []TestCase{
	{"message - none - valid", &cases.MessageNone{Val: &cases.MessageNone_NoneMsg{}}, true},
	{"message - none - valid (unset)", &cases.MessageNone{}, true},

	{"message - disabled - valid", &cases.MessageDisabled{Val: 456}, true},
	{"message - disabled - valid (invalid field)", &cases.MessageDisabled{Val: 0}, true},

	{"message - ignored - valid", &cases.MessageIgnored{Val: 456}, true},
	{"message - ignored - valid (invalid field)", &cases.MessageIgnored{Val: 0}, true},

	{"message - field - valid", &cases.Message{Val: &cases.TestMsg{Const: "foo"}}, true},
	{"message - field - valid (unset)", &cases.Message{}, true},
	{"message - field - invalid", &cases.Message{Val: &cases.TestMsg{}}, false},
	{"message - field - invalid (transitive)", &cases.Message{Val: &cases.TestMsg{Const: "foo", Nested: &cases.TestMsg{}}}, false},

	{"message - skip - valid", &cases.MessageSkip{Val: &cases.TestMsg{}}, true},

	{"message - required - valid", &cases.MessageRequired{Val: &cases.TestMsg{Const: "foo"}}, true},
	{"message - required - invalid", &cases.MessageRequired{}, false},

	{"message - cross-package embed none - valid", &cases.MessageCrossPackage{Val: &other_package.Embed{Val: 1}}, true},
	{"message - cross-package embed none - valid (nil)", &cases.MessageCrossPackage{}, true},
	{"message - cross-package embed none - valid (empty)", &cases.MessageCrossPackage{Val: &other_package.Embed{}}, false},
	{"message - cross-package embed none - invalid", &cases.MessageCrossPackage{Val: &other_package.Embed{Val: -1}}, false},
}

var repeatedCases = []TestCase{
	{"repeated - none - valid", &cases.RepeatedNone{Val: []int64{1, 2, 3}}, true},

	{"repeated - embed none - valid", &cases.RepeatedEmbedNone{Val: []*cases.Embed{{Val: 1}}}, true},
	{"repeated - embed none - valid (nil)", &cases.RepeatedEmbedNone{}, true},
	{"repeated - embed none - valid (empty)", &cases.RepeatedEmbedNone{Val: []*cases.Embed{}}, true},
	{"repeated - embed none - invalid", &cases.RepeatedEmbedNone{Val: []*cases.Embed{{Val: -1}}}, false},

	{"repeated - cross-package embed none - valid", &cases.RepeatedEmbedCrossPackageNone{Val: []*other_package.Embed{{Val: 1}}}, true},
	{"repeated - cross-package embed none - valid (nil)", &cases.RepeatedEmbedCrossPackageNone{}, true},
	{"repeated - cross-package embed none - valid (empty)", &cases.RepeatedEmbedCrossPackageNone{Val: []*other_package.Embed{}}, true},
	{"repeated - cross-package embed none - invalid", &cases.RepeatedEmbedCrossPackageNone{Val: []*other_package.Embed{{Val: -1}}}, false},

	{"repeated - min - valid", &cases.RepeatedMin{Val: []*cases.Embed{{Val: 1}, {Val: 2}, {Val: 3}}}, true},
	{"repeated - min - valid (equal)", &cases.RepeatedMin{Val: []*cases.Embed{{Val: 1}, {Val: 2}}}, true},
	{"repeated - min - invalid", &cases.RepeatedMin{Val: []*cases.Embed{{Val: 1}}}, false},
	{"repeated - min - invalid (element)", &cases.RepeatedMin{Val: []*cases.Embed{{Val: 1}, {Val: -1}}}, false},

	{"repeated - max - valid", &cases.RepeatedMax{Val: []float64{1, 2}}, true},
	{"repeated - max - valid (equal)", &cases.RepeatedMax{Val: []float64{1, 2, 3}}, true},
	{"repeated - max - invalid", &cases.RepeatedMax{Val: []float64{1, 2, 3, 4}}, false},

	{"repeated - min/max - valid", &cases.RepeatedMinMax{Val: []int32{1, 2, 3}}, true},
	{"repeated - min/max - valid (min)", &cases.RepeatedMinMax{Val: []int32{1, 2}}, true},
	{"repeated - min/max - valid (max)", &cases.RepeatedMinMax{Val: []int32{1, 2, 3, 4}}, true},
	{"repeated - min/max - invalid (below)", &cases.RepeatedMinMax{Val: []int32{}}, false},
	{"repeated - min/max - invalid (above)", &cases.RepeatedMinMax{Val: []int32{1, 2, 3, 4, 5}}, false},

	{"repeated - exact - valid", &cases.RepeatedExact{Val: []uint32{1, 2, 3}}, true},
	{"repeated - exact - invalid (below)", &cases.RepeatedExact{Val: []uint32{1, 2}}, false},
	{"repeated - exact - invalid (above)", &cases.RepeatedExact{Val: []uint32{1, 2, 3, 4}}, false},

	{"repeated - unique - valid", &cases.RepeatedUnique{Val: []string{"foo", "bar", "baz"}}, true},
	{"repeated - unique - valid (empty)", &cases.RepeatedUnique{}, true},
	{"repeated - unique - valid (case sensitivity)", &cases.RepeatedUnique{Val: []string{"foo", "Foo"}}, true},
	{"repeated - unique - invalid", &cases.RepeatedUnique{Val: []string{"foo", "bar", "foo", "baz"}}, false},

	{"repeated - items - valid", &cases.RepeatedItemRule{Val: []float32{1, 2, 3}}, true},
	{"repeated - items - valid (empty)", &cases.RepeatedItemRule{Val: []float32{}}, true},
	{"repeated - items - valid (pattern)", &cases.RepeatedItemPattern{Val: []string{"Alpha", "Beta123"}}, true},
	{"repeated - items - invalid", &cases.RepeatedItemRule{Val: []float32{1, -2, 3}}, false},
	{"repeated - items - invalid (pattern)", &cases.RepeatedItemPattern{Val: []string{"Alpha", "!@#$%^&*()"}}, false},
	{"repeated - items - invalid (in)", &cases.RepeatedItemIn{Val: []string{"baz"}}, false},
	{"repeated - items - valid (in)", &cases.RepeatedItemIn{Val: []string{"foo"}}, true},
	{"repeated - items - invalid (not_in)", &cases.RepeatedItemNotIn{Val: []string{"foo"}}, false},
	{"repeated - items - valid (not_in)", &cases.RepeatedItemNotIn{Val: []string{"baz"}}, true},

	{"repeated - items - invalid (enum in)", &cases.RepeatedEnumIn{Val: []cases.AnEnum{1}}, false},
	{"repeated - items - valid (enum in)", &cases.RepeatedEnumIn{Val: []cases.AnEnum{0}}, true},
	{"repeated - items - invalid (enum not_in)", &cases.RepeatedEnumNotIn{Val: []cases.AnEnum{0}}, false},
	{"repeated - items - valid (enum not_in)", &cases.RepeatedEnumNotIn{Val: []cases.AnEnum{1}}, true},
	{"repeated - items - invalid (embedded enum in)", &cases.RepeatedEmbeddedEnumIn{Val: []cases.RepeatedEmbeddedEnumIn_AnotherInEnum{1}}, false},
	{"repeated - items - valid (embedded enum in)", &cases.RepeatedEmbeddedEnumIn{Val: []cases.RepeatedEmbeddedEnumIn_AnotherInEnum{0}}, true},
	{"repeated - items - invalid (embedded enum not_in)", &cases.RepeatedEmbeddedEnumNotIn{Val: []cases.RepeatedEmbeddedEnumNotIn_AnotherNotInEnum{0}}, false},
	{"repeated - items - valid (embedded enum not_in)", &cases.RepeatedEmbeddedEnumNotIn{Val: []cases.RepeatedEmbeddedEnumNotIn_AnotherNotInEnum{1}}, true},

	{"repeated - embed skip - valid", &cases.RepeatedEmbedSkip{Val: []*cases.Embed{{Val: 1}}}, true},
	{"repeated - embed skip - valid (invalid element)", &cases.RepeatedEmbedSkip{Val: []*cases.Embed{{Val: -1}}}, true},
	{"repeated - min and items len - valid", &cases.RepeatedMinAndItemLen{Val: []string{"aaa", "bbb"}}, true},
	{"repeated - min and items len - invalid (min)", &cases.RepeatedMinAndItemLen{Val: []string{}}, false},
	{"repeated - min and items len - invalid (len)", &cases.RepeatedMinAndItemLen{Val: []string{"x"}}, false},
	{"repeated - min and max items len - valid", &cases.RepeatedMinAndMaxItemLen{Val: []string{"aaa", "bbb"}}, true},
	{"repeated - min and max items len - invalid (min_len)", &cases.RepeatedMinAndMaxItemLen{}, false},
	{"repeated - min and max items len - invalid (max_len)", &cases.RepeatedMinAndMaxItemLen{Val: []string{"aaa", "bbb", "ccc", "ddd"}}, false},

	{"repeated - duration - gte - valid", &cases.RepeatedDuration{Val: []*duration.Duration{{Seconds: 3}}}, true},
	{"repeated - duration - gte - valid (empty)", &cases.RepeatedDuration{}, true},
	{"repeated - duration - gte - valid (equal)", &cases.RepeatedDuration{Val: []*duration.Duration{{Nanos: 1000000}}}, true},
	{"repeated - duration - gte - invalid", &cases.RepeatedDuration{Val: []*duration.Duration{{Seconds: -1}}}, false},
}

var mapCases = []TestCase{
	{"map - none - valid", &cases.MapNone{Val: map[uint32]bool{123: true, 456: false}}, true},

	{"map - min pairs - valid", &cases.MapMin{Val: map[int32]float32{1: 2, 3: 4, 5: 6}}, true},
	{"map - min pairs - valid (equal)", &cases.MapMin{Val: map[int32]float32{1: 2, 3: 4}}, true},
	{"map - min pairs - invalid", &cases.MapMin{Val: map[int32]float32{1: 2}}, false},

	{"map - max pairs - valid", &cases.MapMax{Val: map[int64]float64{1: 2, 3: 4}}, true},
	{"map - max pairs - valid (equal)", &cases.MapMax{Val: map[int64]float64{1: 2, 3: 4, 5: 6}}, true},
	{"map - max pairs - invalid", &cases.MapMax{Val: map[int64]float64{1: 2, 3: 4, 5: 6, 7: 8}}, false},

	{"map - min/max - valid", &cases.MapMinMax{Val: map[string]bool{"a": true, "b": false, "c": true}}, true},
	{"map - min/max - valid (min)", &cases.MapMinMax{Val: map[string]bool{"a": true, "b": false}}, true},
	{"map - min/max - valid (max)", &cases.MapMinMax{Val: map[string]bool{"a": true, "b": false, "c": true, "d": false}}, true},
	{"map - min/max - invalid (below)", &cases.MapMinMax{Val: map[string]bool{}}, false},
	{"map - min/max - invalid (above)", &cases.MapMinMax{Val: map[string]bool{"a": true, "b": false, "c": true, "d": false, "e": true}}, false},

	{"map - exact - valid", &cases.MapExact{Val: map[uint64]string{1: "a", 2: "b", 3: "c"}}, true},
	{"map - exact - invalid (below)", &cases.MapExact{Val: map[uint64]string{1: "a", 2: "b"}}, false},
	{"map - exact - invalid (above)", &cases.MapExact{Val: map[uint64]string{1: "a", 2: "b", 3: "c", 4: "d"}}, false},

	{"map - no sparse - valid", &cases.MapNoSparse{Val: map[uint32]*cases.MapNoSparse_Msg{1: {}, 2: {}}}, true},
	{"map - no sparse - valid (empty)", &cases.MapNoSparse{Val: map[uint32]*cases.MapNoSparse_Msg{}}, true},
	// sparse maps are no longer supported, so this case is no longer possible
	//{"map - no sparse - invalid", &cases.MapNoSparse{Val: map[uint32]*cases.MapNoSparse_Msg{1: {}, 2: nil}}, false},

	{"map - keys - valid", &cases.MapKeys{Val: map[int64]string{-1: "a", -2: "b"}}, true},
	{"map - keys - valid (empty)", &cases.MapKeys{Val: map[int64]string{}}, true},
	{"map - keys - valid (pattern)", &cases.MapKeysPattern{Val: map[string]string{"A": "a"}}, true},
	{"map - keys - invalid", &cases.MapKeys{Val: map[int64]string{1: "a"}}, false},
	{"map - keys - invalid (pattern)", &cases.MapKeysPattern{Val: map[string]string{"A": "a", "!@#$%^&*()": "b"}}, false},

	{"map - values - valid", &cases.MapValues{Val: map[string]string{"a": "Alpha", "b": "Beta"}}, true},
	{"map - values - valid (empty)", &cases.MapValues{Val: map[string]string{}}, true},
	{"map - values - valid (pattern)", &cases.MapValuesPattern{Val: map[string]string{"a": "A"}}, true},
	{"map - values - invalid", &cases.MapValues{Val: map[string]string{"a": "A", "b": "B"}}, false},
	{"map - values - invalid (pattern)", &cases.MapValuesPattern{Val: map[string]string{"a": "A", "b": "!@#$%^&*()"}}, false},

	{"map - recursive - valid", &cases.MapRecursive{Val: map[uint32]*cases.MapRecursive_Msg{1: {Val: "abc"}}}, true},
	{"map - recursive - invalid", &cases.MapRecursive{Val: map[uint32]*cases.MapRecursive_Msg{1: {}}}, false},
}

var oneofCases = []TestCase{
	{"oneof - none - valid", &cases.OneOfNone{O: &cases.OneOfNone_X{X: "foo"}}, true},
	{"oneof - none - valid (empty)", &cases.OneOfNone{}, true},

	{"oneof - field - valid (X)", &cases.OneOf{O: &cases.OneOf_X{X: "foobar"}}, true},
	{"oneof - field - valid (Y)", &cases.OneOf{O: &cases.OneOf_Y{Y: 123}}, true},
	{"oneof - field - valid (Z)", &cases.OneOf{O: &cases.OneOf_Z{Z: &cases.TestOneOfMsg{Val: true}}}, true},
	{"oneof - field - valid (empty)", &cases.OneOf{}, true},
	{"oneof - field - invalid (X)", &cases.OneOf{O: &cases.OneOf_X{X: "fizzbuzz"}}, false},
	{"oneof - field - invalid (Y)", &cases.OneOf{O: &cases.OneOf_Y{Y: -1}}, false},
	{"oneof - filed - invalid (Z)", &cases.OneOf{O: &cases.OneOf_Z{Z: &cases.TestOneOfMsg{}}}, false},

	{"oneof - required - valid", &cases.OneOfRequired{O: &cases.OneOfRequired_X{X: ""}}, true},
	{"oneof - require - invalid", &cases.OneOfRequired{}, false},
}

var wrapperCases = []TestCase{
	{"wrapper - none - valid", &cases.WrapperNone{Val: &wrappers.Int32Value{Value: 123}}, true},
	{"wrapper - none - valid (empty)", &cases.WrapperNone{Val: nil}, true},

	{"wrapper - float - valid", &cases.WrapperFloat{Val: &wrappers.FloatValue{Value: 1}}, true},
	{"wrapper - float - valid (empty)", &cases.WrapperFloat{Val: nil}, true},
	{"wrapper - float - invalid", &cases.WrapperFloat{Val: &wrappers.FloatValue{Value: 0}}, false},

	{"wrapper - double - valid", &cases.WrapperDouble{Val: &wrappers.DoubleValue{Value: 1}}, true},
	{"wrapper - double - valid (empty)", &cases.WrapperDouble{Val: nil}, true},
	{"wrapper - double - invalid", &cases.WrapperDouble{Val: &wrappers.DoubleValue{Value: 0}}, false},

	{"wrapper - int64 - valid", &cases.WrapperInt64{Val: &wrappers.Int64Value{Value: 1}}, true},
	{"wrapper - int64 - valid (empty)", &cases.WrapperInt64{Val: nil}, true},
	{"wrapper - int64 - invalid", &cases.WrapperInt64{Val: &wrappers.Int64Value{Value: 0}}, false},

	{"wrapper - int32 - valid", &cases.WrapperInt32{Val: &wrappers.Int32Value{Value: 1}}, true},
	{"wrapper - int32 - valid (empty)", &cases.WrapperInt32{Val: nil}, true},
	{"wrapper - int32 - invalid", &cases.WrapperInt32{Val: &wrappers.Int32Value{Value: 0}}, false},

	{"wrapper - uint64 - valid", &cases.WrapperUInt64{Val: &wrappers.UInt64Value{Value: 1}}, true},
	{"wrapper - uint64 - valid (empty)", &cases.WrapperUInt64{Val: nil}, true},
	{"wrapper - uint64 - invalid", &cases.WrapperUInt64{Val: &wrappers.UInt64Value{Value: 0}}, false},

	{"wrapper - uint32 - valid", &cases.WrapperUInt32{Val: &wrappers.UInt32Value{Value: 1}}, true},
	{"wrapper - uint32 - valid (empty)", &cases.WrapperUInt32{Val: nil}, true},
	{"wrapper - uint32 - invalid", &cases.WrapperUInt32{Val: &wrappers.UInt32Value{Value: 0}}, false},

	{"wrapper - bool - valid", &cases.WrapperBool{Val: &wrappers.BoolValue{Value: true}}, true},
	{"wrapper - bool - valid (empty)", &cases.WrapperBool{Val: nil}, true},
	{"wrapper - bool - invalid", &cases.WrapperBool{Val: &wrappers.BoolValue{Value: false}}, false},

	{"wrapper - string - valid", &cases.WrapperString{Val: &wrappers.StringValue{Value: "foobar"}}, true},
	{"wrapper - string - valid (empty)", &cases.WrapperString{Val: nil}, true},
	{"wrapper - string - invalid", &cases.WrapperString{Val: &wrappers.StringValue{Value: "fizzbuzz"}}, false},

	{"wrapper - bytes - valid", &cases.WrapperBytes{Val: &wrappers.BytesValue{Value: []byte("foo")}}, true},
	{"wrapper - bytes - valid (empty)", &cases.WrapperBytes{Val: nil}, true},
	{"wrapper - bytes - invalid", &cases.WrapperBytes{Val: &wrappers.BytesValue{Value: []byte("x")}}, false},

	{"wrapper - required - string - valid", &cases.WrapperRequiredString{Val: &wrappers.StringValue{Value: "bar"}}, true},
	{"wrapper - required - string - invalid", &cases.WrapperRequiredString{Val: &wrappers.StringValue{Value: "foo"}}, false},
	{"wrapper - required - string - invalid (empty)", &cases.WrapperRequiredString{}, false},

	{"wrapper - required - string (empty) - valid", &cases.WrapperRequiredEmptyString{Val: &wrappers.StringValue{Value: ""}}, true},
	{"wrapper - required - string (empty) - invalid", &cases.WrapperRequiredEmptyString{Val: &wrappers.StringValue{Value: "foo"}}, false},
	{"wrapper - required - string (empty) - invalid (empty)", &cases.WrapperRequiredEmptyString{}, false},

	{"wrapper - optional - string (uuid) - valid", &cases.WrapperOptionalUuidString{Val: &wrappers.StringValue{Value: "8b72987b-024a-43b3-b4cf-647a1f925c5d"}}, true},
	{"wrapper - optional - string (uuid) - valid (empty)", &cases.WrapperOptionalUuidString{}, true},
	{"wrapper - optional - string (uuid) - invalid", &cases.WrapperOptionalUuidString{Val: &wrappers.StringValue{Value: "foo"}}, false},

	{"wrapper - required - float - valid", &cases.WrapperRequiredFloat{Val: &wrappers.FloatValue{Value: 1}}, true},
	{"wrapper - required - float - invalid", &cases.WrapperRequiredFloat{Val: &wrappers.FloatValue{Value: -5}}, false},
	{"wrapper - required - float - invalid (empty)", &cases.WrapperRequiredFloat{}, false},
}

var durationCases = []TestCase{
	{"duration - none - valid", &cases.DurationNone{Val: &duration.Duration{Seconds: 123}}, true},

	{"duration - required - valid", &cases.DurationRequired{Val: &duration.Duration{}}, true},
	{"duration - required - invalid", &cases.DurationRequired{Val: nil}, false},

	{"duration - const - valid", &cases.DurationConst{Val: &duration.Duration{Seconds: 3}}, true},
	{"duration - const - valid (empty)", &cases.DurationConst{}, true},
	{"duration - const - invalid", &cases.DurationConst{Val: &duration.Duration{Nanos: 3}}, false},

	{"duration - in - valid", &cases.DurationIn{Val: &duration.Duration{Seconds: 1}}, true},
	{"duration - in - valid (empty)", &cases.DurationIn{}, true},
	{"duration - in - invalid", &cases.DurationIn{Val: &duration.Duration{}}, false},

	{"duration - not in - valid", &cases.DurationNotIn{Val: &duration.Duration{Nanos: 1}}, true},
	{"duration - not in - valid (empty)", &cases.DurationNotIn{}, true},
	{"duration - not in - invalid", &cases.DurationNotIn{Val: &duration.Duration{}}, false},

	{"duration - lt - valid", &cases.DurationLT{Val: &duration.Duration{Nanos: -1}}, true},
	{"duration - lt - valid (empty)", &cases.DurationLT{}, true},
	{"duration - lt - invalid (equal)", &cases.DurationLT{Val: &duration.Duration{}}, false},
	{"duration - lt - invalid", &cases.DurationLT{Val: &duration.Duration{Seconds: 1}}, false},

	{"duration - lte - valid", &cases.DurationLTE{Val: &duration.Duration{}}, true},
	{"duration - lte - valid (empty)", &cases.DurationLTE{}, true},
	{"duration - lte - valid (equal)", &cases.DurationLTE{Val: &duration.Duration{Seconds: 1}}, true},
	{"duration - lte - invalid", &cases.DurationLTE{Val: &duration.Duration{Seconds: 1, Nanos: 1}}, false},

	{"duration - gt - valid", &cases.DurationGT{Val: &duration.Duration{Seconds: 1}}, true},
	{"duration - gt - valid (empty)", &cases.DurationGT{}, true},
	{"duration - gt - invalid (equal)", &cases.DurationGT{Val: &duration.Duration{Nanos: 1000}}, false},
	{"duration - gt - invalid", &cases.DurationGT{Val: &duration.Duration{}}, false},

	{"duration - gte - valid", &cases.DurationGTE{Val: &duration.Duration{Seconds: 3}}, true},
	{"duration - gte - valid (empty)", &cases.DurationGTE{}, true},
	{"duration - gte - valid (equal)", &cases.DurationGTE{Val: &duration.Duration{Nanos: 1000000}}, true},
	{"duration - gte - invalid", &cases.DurationGTE{Val: &duration.Duration{Seconds: -1}}, false},

	{"duration - gt & lt - valid", &cases.DurationGTLT{Val: &duration.Duration{Nanos: 1000}}, true},
	{"duration - gt & lt - valid (empty)", &cases.DurationGTLT{}, true},
	{"duration - gt & lt - invalid (above)", &cases.DurationGTLT{Val: &duration.Duration{Seconds: 1000}}, false},
	{"duration - gt & lt - invalid (below)", &cases.DurationGTLT{Val: &duration.Duration{Nanos: -1000}}, false},
	{"duration - gt & lt - invalid (max)", &cases.DurationGTLT{Val: &duration.Duration{Seconds: 1}}, false},
	{"duration - gt & lt - invalid (min)", &cases.DurationGTLT{Val: &duration.Duration{}}, false},

	{"duration - exclusive gt & lt - valid (empty)", &cases.DurationExLTGT{}, true},
	{"duration - exclusive gt & lt - valid (above)", &cases.DurationExLTGT{Val: &duration.Duration{Seconds: 2}}, true},
	{"duration - exclusive gt & lt - valid (below)", &cases.DurationExLTGT{Val: &duration.Duration{Nanos: -1}}, true},
	{"duration - exclusive gt & lt - invalid", &cases.DurationExLTGT{Val: &duration.Duration{Nanos: 1000}}, false},
	{"duration - exclusive gt & lt - invalid (max)", &cases.DurationExLTGT{Val: &duration.Duration{Seconds: 1}}, false},
	{"duration - exclusive gt & lt - invalid (min)", &cases.DurationExLTGT{Val: &duration.Duration{}}, false},

	{"duration - gte & lte - valid", &cases.DurationGTELTE{Val: &duration.Duration{Seconds: 60, Nanos: 1}}, true},
	{"duration - gte & lte - valid (empty)", &cases.DurationGTELTE{}, true},
	{"duration - gte & lte - valid (max)", &cases.DurationGTELTE{Val: &duration.Duration{Seconds: 3600}}, true},
	{"duration - gte & lte - valid (min)", &cases.DurationGTELTE{Val: &duration.Duration{Seconds: 60}}, true},
	{"duration - gte & lte - invalid (above)", &cases.DurationGTELTE{Val: &duration.Duration{Seconds: 3600, Nanos: 1}}, false},
	{"duration - gte & lte - invalid (below)", &cases.DurationGTELTE{Val: &duration.Duration{Seconds: 59}}, false},

	{"duration - gte & lte - valid (empty)", &cases.DurationExGTELTE{}, true},
	{"duration - exclusive gte & lte - valid (above)", &cases.DurationExGTELTE{Val: &duration.Duration{Seconds: 3601}}, true},
	{"duration - exclusive gte & lte - valid (below)", &cases.DurationExGTELTE{Val: &duration.Duration{}}, true},
	{"duration - exclusive gte & lte - valid (max)", &cases.DurationExGTELTE{Val: &duration.Duration{Seconds: 3600}}, true},
	{"duration - exclusive gte & lte - valid (min)", &cases.DurationExGTELTE{Val: &duration.Duration{Seconds: 60}}, true},
	{"duration - exclusive gte & lte - invalid", &cases.DurationExGTELTE{Val: &duration.Duration{Seconds: 61}}, false},
	{"duration - fields with other fields - invalid other field", &cases.DurationFieldWithOtherFields{DurationVal: nil, IntVal: 12}, false},
}

var timestampCases = []TestCase{
	{"timestamp - none - valid", &cases.TimestampNone{Val: &timestamp.Timestamp{Seconds: 123}}, true},

	{"timestamp - required - valid", &cases.TimestampRequired{Val: &timestamp.Timestamp{}}, true},
	{"timestamp - required - invalid", &cases.TimestampRequired{Val: nil}, false},

	{"timestamp - const - valid", &cases.TimestampConst{Val: &timestamp.Timestamp{Seconds: 3}}, true},
	{"timestamp - const - valid (empty)", &cases.TimestampConst{}, true},
	{"timestamp - const - invalid", &cases.TimestampConst{Val: &timestamp.Timestamp{Nanos: 3}}, false},

	{"timestamp - lt - valid", &cases.TimestampLT{Val: &timestamp.Timestamp{Seconds: -1}}, true},
	{"timestamp - lt - valid (empty)", &cases.TimestampLT{}, true},
	{"timestamp - lt - invalid (equal)", &cases.TimestampLT{Val: &timestamp.Timestamp{}}, false},
	{"timestamp - lt - invalid", &cases.TimestampLT{Val: &timestamp.Timestamp{Seconds: 1}}, false},

	{"timestamp - lte - valid", &cases.TimestampLTE{Val: &timestamp.Timestamp{}}, true},
	{"timestamp - lte - valid (empty)", &cases.TimestampLTE{}, true},
	{"timestamp - lte - valid (equal)", &cases.TimestampLTE{Val: &timestamp.Timestamp{Seconds: 1}}, true},
	{"timestamp - lte - invalid", &cases.TimestampLTE{Val: &timestamp.Timestamp{Seconds: 1, Nanos: 1}}, false},

	{"timestamp - gt - valid", &cases.TimestampGT{Val: &timestamp.Timestamp{Seconds: 1}}, true},
	{"timestamp - gt - valid (empty)", &cases.TimestampGT{}, true},
	{"timestamp - gt - invalid (equal)", &cases.TimestampGT{Val: &timestamp.Timestamp{Nanos: 1000}}, false},
	{"timestamp - gt - invalid", &cases.TimestampGT{Val: &timestamp.Timestamp{}}, false},

	{"timestamp - gte - valid", &cases.TimestampGTE{Val: &timestamp.Timestamp{Seconds: 3}}, true},
	{"timestamp - gte - valid (empty)", &cases.TimestampGTE{}, true},
	{"timestamp - gte - valid (equal)", &cases.TimestampGTE{Val: &timestamp.Timestamp{Nanos: 1000000}}, true},
	{"timestamp - gte - invalid", &cases.TimestampGTE{Val: &timestamp.Timestamp{Seconds: -1}}, false},

	{"timestamp - gt & lt - valid", &cases.TimestampGTLT{Val: &timestamp.Timestamp{Nanos: 1000}}, true},
	{"timestamp - gt & lt - valid (empty)", &cases.TimestampGTLT{}, true},
	{"timestamp - gt & lt - invalid (above)", &cases.TimestampGTLT{Val: &timestamp.Timestamp{Seconds: 1000}}, false},
	{"timestamp - gt & lt - invalid (below)", &cases.TimestampGTLT{Val: &timestamp.Timestamp{Seconds: -1000}}, false},
	{"timestamp - gt & lt - invalid (max)", &cases.TimestampGTLT{Val: &timestamp.Timestamp{Seconds: 1}}, false},
	{"timestamp - gt & lt - invalid (min)", &cases.TimestampGTLT{Val: &timestamp.Timestamp{}}, false},

	{"timestamp - exclusive gt & lt - valid (empty)", &cases.TimestampExLTGT{}, true},
	{"timestamp - exclusive gt & lt - valid (above)", &cases.TimestampExLTGT{Val: &timestamp.Timestamp{Seconds: 2}}, true},
	{"timestamp - exclusive gt & lt - valid (below)", &cases.TimestampExLTGT{Val: &timestamp.Timestamp{Seconds: -1}}, true},
	{"timestamp - exclusive gt & lt - invalid", &cases.TimestampExLTGT{Val: &timestamp.Timestamp{Nanos: 1000}}, false},
	{"timestamp - exclusive gt & lt - invalid (max)", &cases.TimestampExLTGT{Val: &timestamp.Timestamp{Seconds: 1}}, false},
	{"timestamp - exclusive gt & lt - invalid (min)", &cases.TimestampExLTGT{Val: &timestamp.Timestamp{}}, false},

	{"timestamp - gte & lte - valid", &cases.TimestampGTELTE{Val: &timestamp.Timestamp{Seconds: 60, Nanos: 1}}, true},
	{"timestamp - gte & lte - valid (empty)", &cases.TimestampGTELTE{}, true},
	{"timestamp - gte & lte - valid (max)", &cases.TimestampGTELTE{Val: &timestamp.Timestamp{Seconds: 3600}}, true},
	{"timestamp - gte & lte - valid (min)", &cases.TimestampGTELTE{Val: &timestamp.Timestamp{Seconds: 60}}, true},
	{"timestamp - gte & lte - invalid (above)", &cases.TimestampGTELTE{Val: &timestamp.Timestamp{Seconds: 3600, Nanos: 1}}, false},
	{"timestamp - gte & lte - invalid (below)", &cases.TimestampGTELTE{Val: &timestamp.Timestamp{Seconds: 59}}, false},

	{"timestamp - gte & lte - valid (empty)", &cases.TimestampExGTELTE{}, true},
	{"timestamp - exclusive gte & lte - valid (above)", &cases.TimestampExGTELTE{Val: &timestamp.Timestamp{Seconds: 3601}}, true},
	{"timestamp - exclusive gte & lte - valid (below)", &cases.TimestampExGTELTE{Val: &timestamp.Timestamp{}}, true},
	{"timestamp - exclusive gte & lte - valid (max)", &cases.TimestampExGTELTE{Val: &timestamp.Timestamp{Seconds: 3600}}, true},
	{"timestamp - exclusive gte & lte - valid (min)", &cases.TimestampExGTELTE{Val: &timestamp.Timestamp{Seconds: 60}}, true},
	{"timestamp - exclusive gte & lte - invalid", &cases.TimestampExGTELTE{Val: &timestamp.Timestamp{Seconds: 61}}, false},

	{"timestamp - lt now - valid", &cases.TimestampLTNow{Val: &timestamp.Timestamp{}}, true},
	{"timestamp - lt now - valid (empty)", &cases.TimestampLTNow{}, true},
	{"timestamp - lt now - invalid", &cases.TimestampLTNow{Val: &timestamp.Timestamp{Seconds: time.Now().Unix() + 7200}}, false},

	{"timestamp - gt now - valid", &cases.TimestampGTNow{Val: &timestamp.Timestamp{Seconds: time.Now().Unix() + 7200}}, true},
	{"timestamp - gt now - valid (empty)", &cases.TimestampGTNow{}, true},
	{"timestamp - gt now - invalid", &cases.TimestampGTNow{Val: &timestamp.Timestamp{}}, false},

	{"timestamp - within - valid", &cases.TimestampWithin{Val: ptypes.TimestampNow()}, true},
	{"timestamp - within - valid (empty)", &cases.TimestampWithin{}, true},
	{"timestamp - within - invalid (below)", &cases.TimestampWithin{Val: &timestamp.Timestamp{}}, false},
	{"timestamp - within - invalid (above)", &cases.TimestampWithin{Val: &timestamp.Timestamp{Seconds: time.Now().Unix() + 7200}}, false},

	{"timestamp - lt now within - valid", &cases.TimestampLTNowWithin{Val: &timestamp.Timestamp{Seconds: time.Now().Unix() - 1800}}, true},
	{"timestamp - lt now within - valid (empty)", &cases.TimestampLTNowWithin{}, true},
	{"timestamp - lt now within - invalid (lt)", &cases.TimestampLTNowWithin{Val: &timestamp.Timestamp{Seconds: time.Now().Unix() + 1800}}, false},
	{"timestamp - lt now within - invalid (within)", &cases.TimestampLTNowWithin{Val: &timestamp.Timestamp{Seconds: time.Now().Unix() - 7200}}, false},

	{"timestamp - gt now within - valid", &cases.TimestampGTNowWithin{Val: &timestamp.Timestamp{Seconds: time.Now().Unix() + 1800}}, true},
	{"timestamp - gt now within - valid (empty)", &cases.TimestampGTNowWithin{}, true},
	{"timestamp - gt now within - invalid (gt)", &cases.TimestampGTNowWithin{Val: &timestamp.Timestamp{Seconds: time.Now().Unix() - 1800}}, false},
	{"timestamp - gt now within - invalid (within)", &cases.TimestampGTNowWithin{Val: &timestamp.Timestamp{Seconds: time.Now().Unix() + 7200}}, false},
}

var anyCases = []TestCase{
	{"any - none - valid", &cases.AnyNone{Val: &any.Any{}}, true},

	{"any - required - valid", &cases.AnyRequired{Val: &any.Any{}}, true},
	{"any - required - invalid", &cases.AnyRequired{Val: nil}, false},

	{"any - in - valid", &cases.AnyIn{Val: &any.Any{TypeUrl: "type.googleapis.com/google.protobuf.Duration"}}, true},
	{"any - in - valid (empty)", &cases.AnyIn{}, true},
	{"any - in - invalid", &cases.AnyIn{Val: &any.Any{TypeUrl: "type.googleapis.com/google.protobuf.Timestamp"}}, false},

	{"any - not in - valid", &cases.AnyNotIn{Val: &any.Any{TypeUrl: "type.googleapis.com/google.protobuf.Duration"}}, true},
	{"any - not in - valid (empty)", &cases.AnyNotIn{}, true},
	{"any - not in - invalid", &cases.AnyNotIn{Val: &any.Any{TypeUrl: "type.googleapis.com/google.protobuf.Timestamp"}}, false},
}

var kitchenSink = []TestCase{
	{"kitchensink - field - valid", &cases.KitchenSinkMessage{Val: &cases.ComplexTestMsg{Const: "abcd", IntConst: 5, BoolConst: false, FloatVal: &wrappers.FloatValue{Value: 1}, DurVal: &duration.Duration{Seconds: 3}, TsVal: &timestamp.Timestamp{Seconds: 17}, FloatConst: 7, DoubleIn: 123, EnumConst: cases.ComplexTestEnum_ComplexTWO, AnyVal: &any.Any{TypeUrl: "type.googleapis.com/google.protobuf.Duration"}, RepTsVal: []*timestamp.Timestamp{{Seconds: 3}}, MapVal: map[int32]string{-1: "a", -2: "b"}, BytesVal: []byte("\x00\x99"), O: &cases.ComplexTestMsg_X{X: "foobar"}}}, true},
	{"kitchensink - valid (unset)", &cases.KitchenSinkMessage{}, true},
	{"kitchensink - field - invalid", &cases.KitchenSinkMessage{Val: &cases.ComplexTestMsg{}}, false},
	{"kitchensink - field - embedded - invalid", &cases.KitchenSinkMessage{Val: &cases.ComplexTestMsg{Another: &cases.ComplexTestMsg{}}}, false},
	{"kitchensink - field - invalid (transitive)", &cases.KitchenSinkMessage{Val: &cases.ComplexTestMsg{Const: "abcd", BoolConst: true, Nested: &cases.ComplexTestMsg{}}}, false},
}
