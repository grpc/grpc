// Code generated by protoc-gen-go. DO NOT EDIT.
// source: src/proto/grpc/testing/report_qps_scenario_service.proto

package grpc_testing

import (
	context "context"
	fmt "fmt"
	proto "github.com/golang/protobuf/proto"
	grpc "google.golang.org/grpc"
	codes "google.golang.org/grpc/codes"
	status "google.golang.org/grpc/status"
	math "math"
)

// Reference imports to suppress errors if they are not otherwise used.
var _ = proto.Marshal
var _ = fmt.Errorf
var _ = math.Inf

// This is a compile-time assertion to ensure that this generated file
// is compatible with the proto package it is being compiled against.
// A compilation error at this line likely means your copy of the
// proto package needs to be updated.
const _ = proto.ProtoPackageIsVersion3 // please upgrade the proto package

func init() {
	proto.RegisterFile("src/proto/grpc/testing/report_qps_scenario_service.proto", fileDescriptor_294960e8de0508df)
}

var fileDescriptor_294960e8de0508df = []byte{
	// 151 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xe2, 0xb2, 0x28, 0x2e, 0x4a, 0xd6,
	0x2f, 0x28, 0xca, 0x2f, 0xc9, 0xd7, 0x4f, 0x2f, 0x2a, 0x48, 0xd6, 0x2f, 0x49, 0x2d, 0x2e, 0xc9,
	0xcc, 0x4b, 0xd7, 0x2f, 0x4a, 0x2d, 0xc8, 0x2f, 0x2a, 0x89, 0x2f, 0x2c, 0x28, 0x8e, 0x2f, 0x4e,
	0x4e, 0xcd, 0x4b, 0x2c, 0xca, 0xcc, 0x8f, 0x2f, 0x4e, 0x2d, 0x2a, 0xcb, 0x4c, 0x4e, 0xd5, 0x03,
	0x2b, 0x17, 0xe2, 0x01, 0xa9, 0xd7, 0x83, 0xaa, 0x97, 0x52, 0xc1, 0x61, 0x4e, 0x72, 0x7e, 0x5e,
	0x49, 0x51, 0x7e, 0x0e, 0x44, 0x8f, 0x51, 0x1c, 0x97, 0x44, 0x10, 0xd8, 0xe0, 0xc0, 0x82, 0xe2,
	0x60, 0xa8, 0xb1, 0xc1, 0x10, 0x53, 0x85, 0x9c, 0xb8, 0xf8, 0x20, 0x72, 0x30, 0x09, 0x21, 0x19,
	0x3d, 0x64, 0x2b, 0xf4, 0x60, 0xe2, 0x41, 0xa9, 0xc5, 0xa5, 0x39, 0x25, 0x52, 0x42, 0xa8, 0xb2,
	0x61, 0xf9, 0x99, 0x29, 0x49, 0x6c, 0x60, 0x6b, 0x8c, 0x01, 0x01, 0x00, 0x00, 0xff, 0xff, 0x0b,
	0x80, 0xdb, 0x9d, 0xd6, 0x00, 0x00, 0x00,
}

// Reference imports to suppress errors if they are not otherwise used.
var _ context.Context
var _ grpc.ClientConn

// This is a compile-time assertion to ensure that this generated file
// is compatible with the grpc package it is being compiled against.
const _ = grpc.SupportPackageIsVersion4

// ReportQpsScenarioServiceClient is the client API for ReportQpsScenarioService service.
//
// For semantics around ctx use and closing/ending streaming RPCs, please refer to https://godoc.org/google.golang.org/grpc#ClientConn.NewStream.
type ReportQpsScenarioServiceClient interface {
	// Report results of a QPS test benchmark scenario.
	ReportScenario(ctx context.Context, in *ScenarioResult, opts ...grpc.CallOption) (*Void, error)
}

type reportQpsScenarioServiceClient struct {
	cc *grpc.ClientConn
}

func NewReportQpsScenarioServiceClient(cc *grpc.ClientConn) ReportQpsScenarioServiceClient {
	return &reportQpsScenarioServiceClient{cc}
}

func (c *reportQpsScenarioServiceClient) ReportScenario(ctx context.Context, in *ScenarioResult, opts ...grpc.CallOption) (*Void, error) {
	out := new(Void)
	err := c.cc.Invoke(ctx, "/grpc.testing.ReportQpsScenarioService/ReportScenario", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

// ReportQpsScenarioServiceServer is the server API for ReportQpsScenarioService service.
type ReportQpsScenarioServiceServer interface {
	// Report results of a QPS test benchmark scenario.
	ReportScenario(context.Context, *ScenarioResult) (*Void, error)
}

// UnimplementedReportQpsScenarioServiceServer can be embedded to have forward compatible implementations.
type UnimplementedReportQpsScenarioServiceServer struct {
}

func (*UnimplementedReportQpsScenarioServiceServer) ReportScenario(ctx context.Context, req *ScenarioResult) (*Void, error) {
	return nil, status.Errorf(codes.Unimplemented, "method ReportScenario not implemented")
}

func RegisterReportQpsScenarioServiceServer(s *grpc.Server, srv ReportQpsScenarioServiceServer) {
	s.RegisterService(&_ReportQpsScenarioService_serviceDesc, srv)
}

func _ReportQpsScenarioService_ReportScenario_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(ScenarioResult)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(ReportQpsScenarioServiceServer).ReportScenario(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/grpc.testing.ReportQpsScenarioService/ReportScenario",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(ReportQpsScenarioServiceServer).ReportScenario(ctx, req.(*ScenarioResult))
	}
	return interceptor(ctx, in, info, handler)
}

var _ReportQpsScenarioService_serviceDesc = grpc.ServiceDesc{
	ServiceName: "grpc.testing.ReportQpsScenarioService",
	HandlerType: (*ReportQpsScenarioServiceServer)(nil),
	Methods: []grpc.MethodDesc{
		{
			MethodName: "ReportScenario",
			Handler:    _ReportQpsScenarioService_ReportScenario_Handler,
		},
	},
	Streams:  []grpc.StreamDesc{},
	Metadata: "src/proto/grpc/testing/report_qps_scenario_service.proto",
}
