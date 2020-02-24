// Code generated by protoc-gen-go. DO NOT EDIT.
// source: src/proto/grpc/testing/echo_messages.proto

package grpc_testing

import (
	fmt "fmt"
	proto "github.com/golang/protobuf/proto"
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

// Message to be echoed back serialized in trailer.
type DebugInfo struct {
	StackEntries         []string `protobuf:"bytes,1,rep,name=stack_entries,json=stackEntries,proto3" json:"stack_entries,omitempty"`
	Detail               string   `protobuf:"bytes,2,opt,name=detail,proto3" json:"detail,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *DebugInfo) Reset()         { *m = DebugInfo{} }
func (m *DebugInfo) String() string { return proto.CompactTextString(m) }
func (*DebugInfo) ProtoMessage()    {}
func (*DebugInfo) Descriptor() ([]byte, []int) {
	return fileDescriptor_700224c0fbd9d22b, []int{0}
}

func (m *DebugInfo) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_DebugInfo.Unmarshal(m, b)
}
func (m *DebugInfo) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_DebugInfo.Marshal(b, m, deterministic)
}
func (m *DebugInfo) XXX_Merge(src proto.Message) {
	xxx_messageInfo_DebugInfo.Merge(m, src)
}
func (m *DebugInfo) XXX_Size() int {
	return xxx_messageInfo_DebugInfo.Size(m)
}
func (m *DebugInfo) XXX_DiscardUnknown() {
	xxx_messageInfo_DebugInfo.DiscardUnknown(m)
}

var xxx_messageInfo_DebugInfo proto.InternalMessageInfo

func (m *DebugInfo) GetStackEntries() []string {
	if m != nil {
		return m.StackEntries
	}
	return nil
}

func (m *DebugInfo) GetDetail() string {
	if m != nil {
		return m.Detail
	}
	return ""
}

// Error status client expects to see.
type ErrorStatus struct {
	Code                 int32    `protobuf:"varint,1,opt,name=code,proto3" json:"code,omitempty"`
	ErrorMessage         string   `protobuf:"bytes,2,opt,name=error_message,json=errorMessage,proto3" json:"error_message,omitempty"`
	BinaryErrorDetails   string   `protobuf:"bytes,3,opt,name=binary_error_details,json=binaryErrorDetails,proto3" json:"binary_error_details,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ErrorStatus) Reset()         { *m = ErrorStatus{} }
func (m *ErrorStatus) String() string { return proto.CompactTextString(m) }
func (*ErrorStatus) ProtoMessage()    {}
func (*ErrorStatus) Descriptor() ([]byte, []int) {
	return fileDescriptor_700224c0fbd9d22b, []int{1}
}

func (m *ErrorStatus) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ErrorStatus.Unmarshal(m, b)
}
func (m *ErrorStatus) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ErrorStatus.Marshal(b, m, deterministic)
}
func (m *ErrorStatus) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ErrorStatus.Merge(m, src)
}
func (m *ErrorStatus) XXX_Size() int {
	return xxx_messageInfo_ErrorStatus.Size(m)
}
func (m *ErrorStatus) XXX_DiscardUnknown() {
	xxx_messageInfo_ErrorStatus.DiscardUnknown(m)
}

var xxx_messageInfo_ErrorStatus proto.InternalMessageInfo

func (m *ErrorStatus) GetCode() int32 {
	if m != nil {
		return m.Code
	}
	return 0
}

func (m *ErrorStatus) GetErrorMessage() string {
	if m != nil {
		return m.ErrorMessage
	}
	return ""
}

func (m *ErrorStatus) GetBinaryErrorDetails() string {
	if m != nil {
		return m.BinaryErrorDetails
	}
	return ""
}

type RequestParams struct {
	EchoDeadline                  bool         `protobuf:"varint,1,opt,name=echo_deadline,json=echoDeadline,proto3" json:"echo_deadline,omitempty"`
	ClientCancelAfterUs           int32        `protobuf:"varint,2,opt,name=client_cancel_after_us,json=clientCancelAfterUs,proto3" json:"client_cancel_after_us,omitempty"`
	ServerCancelAfterUs           int32        `protobuf:"varint,3,opt,name=server_cancel_after_us,json=serverCancelAfterUs,proto3" json:"server_cancel_after_us,omitempty"`
	EchoMetadata                  bool         `protobuf:"varint,4,opt,name=echo_metadata,json=echoMetadata,proto3" json:"echo_metadata,omitempty"`
	CheckAuthContext              bool         `protobuf:"varint,5,opt,name=check_auth_context,json=checkAuthContext,proto3" json:"check_auth_context,omitempty"`
	ResponseMessageLength         int32        `protobuf:"varint,6,opt,name=response_message_length,json=responseMessageLength,proto3" json:"response_message_length,omitempty"`
	EchoPeer                      bool         `protobuf:"varint,7,opt,name=echo_peer,json=echoPeer,proto3" json:"echo_peer,omitempty"`
	ExpectedClientIdentity        string       `protobuf:"bytes,8,opt,name=expected_client_identity,json=expectedClientIdentity,proto3" json:"expected_client_identity,omitempty"`
	SkipCancelledCheck            bool         `protobuf:"varint,9,opt,name=skip_cancelled_check,json=skipCancelledCheck,proto3" json:"skip_cancelled_check,omitempty"`
	ExpectedTransportSecurityType string       `protobuf:"bytes,10,opt,name=expected_transport_security_type,json=expectedTransportSecurityType,proto3" json:"expected_transport_security_type,omitempty"`
	DebugInfo                     *DebugInfo   `protobuf:"bytes,11,opt,name=debug_info,json=debugInfo,proto3" json:"debug_info,omitempty"`
	ServerDie                     bool         `protobuf:"varint,12,opt,name=server_die,json=serverDie,proto3" json:"server_die,omitempty"`
	BinaryErrorDetails            string       `protobuf:"bytes,13,opt,name=binary_error_details,json=binaryErrorDetails,proto3" json:"binary_error_details,omitempty"`
	ExpectedError                 *ErrorStatus `protobuf:"bytes,14,opt,name=expected_error,json=expectedError,proto3" json:"expected_error,omitempty"`
	ServerSleepUs                 int32        `protobuf:"varint,15,opt,name=server_sleep_us,json=serverSleepUs,proto3" json:"server_sleep_us,omitempty"`
	BackendChannelIdx             int32        `protobuf:"varint,16,opt,name=backend_channel_idx,json=backendChannelIdx,proto3" json:"backend_channel_idx,omitempty"`
	EchoMetadataInitially         bool         `protobuf:"varint,17,opt,name=echo_metadata_initially,json=echoMetadataInitially,proto3" json:"echo_metadata_initially,omitempty"`
	ServerNotifyClientWhenStarted bool         `protobuf:"varint,18,opt,name=server_notify_client_when_started,json=serverNotifyClientWhenStarted,proto3" json:"server_notify_client_when_started,omitempty"`
	XXX_NoUnkeyedLiteral          struct{}     `json:"-"`
	XXX_unrecognized              []byte       `json:"-"`
	XXX_sizecache                 int32        `json:"-"`
}

func (m *RequestParams) Reset()         { *m = RequestParams{} }
func (m *RequestParams) String() string { return proto.CompactTextString(m) }
func (*RequestParams) ProtoMessage()    {}
func (*RequestParams) Descriptor() ([]byte, []int) {
	return fileDescriptor_700224c0fbd9d22b, []int{2}
}

func (m *RequestParams) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_RequestParams.Unmarshal(m, b)
}
func (m *RequestParams) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_RequestParams.Marshal(b, m, deterministic)
}
func (m *RequestParams) XXX_Merge(src proto.Message) {
	xxx_messageInfo_RequestParams.Merge(m, src)
}
func (m *RequestParams) XXX_Size() int {
	return xxx_messageInfo_RequestParams.Size(m)
}
func (m *RequestParams) XXX_DiscardUnknown() {
	xxx_messageInfo_RequestParams.DiscardUnknown(m)
}

var xxx_messageInfo_RequestParams proto.InternalMessageInfo

func (m *RequestParams) GetEchoDeadline() bool {
	if m != nil {
		return m.EchoDeadline
	}
	return false
}

func (m *RequestParams) GetClientCancelAfterUs() int32 {
	if m != nil {
		return m.ClientCancelAfterUs
	}
	return 0
}

func (m *RequestParams) GetServerCancelAfterUs() int32 {
	if m != nil {
		return m.ServerCancelAfterUs
	}
	return 0
}

func (m *RequestParams) GetEchoMetadata() bool {
	if m != nil {
		return m.EchoMetadata
	}
	return false
}

func (m *RequestParams) GetCheckAuthContext() bool {
	if m != nil {
		return m.CheckAuthContext
	}
	return false
}

func (m *RequestParams) GetResponseMessageLength() int32 {
	if m != nil {
		return m.ResponseMessageLength
	}
	return 0
}

func (m *RequestParams) GetEchoPeer() bool {
	if m != nil {
		return m.EchoPeer
	}
	return false
}

func (m *RequestParams) GetExpectedClientIdentity() string {
	if m != nil {
		return m.ExpectedClientIdentity
	}
	return ""
}

func (m *RequestParams) GetSkipCancelledCheck() bool {
	if m != nil {
		return m.SkipCancelledCheck
	}
	return false
}

func (m *RequestParams) GetExpectedTransportSecurityType() string {
	if m != nil {
		return m.ExpectedTransportSecurityType
	}
	return ""
}

func (m *RequestParams) GetDebugInfo() *DebugInfo {
	if m != nil {
		return m.DebugInfo
	}
	return nil
}

func (m *RequestParams) GetServerDie() bool {
	if m != nil {
		return m.ServerDie
	}
	return false
}

func (m *RequestParams) GetBinaryErrorDetails() string {
	if m != nil {
		return m.BinaryErrorDetails
	}
	return ""
}

func (m *RequestParams) GetExpectedError() *ErrorStatus {
	if m != nil {
		return m.ExpectedError
	}
	return nil
}

func (m *RequestParams) GetServerSleepUs() int32 {
	if m != nil {
		return m.ServerSleepUs
	}
	return 0
}

func (m *RequestParams) GetBackendChannelIdx() int32 {
	if m != nil {
		return m.BackendChannelIdx
	}
	return 0
}

func (m *RequestParams) GetEchoMetadataInitially() bool {
	if m != nil {
		return m.EchoMetadataInitially
	}
	return false
}

func (m *RequestParams) GetServerNotifyClientWhenStarted() bool {
	if m != nil {
		return m.ServerNotifyClientWhenStarted
	}
	return false
}

type EchoRequest struct {
	Message              string         `protobuf:"bytes,1,opt,name=message,proto3" json:"message,omitempty"`
	Param                *RequestParams `protobuf:"bytes,2,opt,name=param,proto3" json:"param,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *EchoRequest) Reset()         { *m = EchoRequest{} }
func (m *EchoRequest) String() string { return proto.CompactTextString(m) }
func (*EchoRequest) ProtoMessage()    {}
func (*EchoRequest) Descriptor() ([]byte, []int) {
	return fileDescriptor_700224c0fbd9d22b, []int{3}
}

func (m *EchoRequest) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_EchoRequest.Unmarshal(m, b)
}
func (m *EchoRequest) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_EchoRequest.Marshal(b, m, deterministic)
}
func (m *EchoRequest) XXX_Merge(src proto.Message) {
	xxx_messageInfo_EchoRequest.Merge(m, src)
}
func (m *EchoRequest) XXX_Size() int {
	return xxx_messageInfo_EchoRequest.Size(m)
}
func (m *EchoRequest) XXX_DiscardUnknown() {
	xxx_messageInfo_EchoRequest.DiscardUnknown(m)
}

var xxx_messageInfo_EchoRequest proto.InternalMessageInfo

func (m *EchoRequest) GetMessage() string {
	if m != nil {
		return m.Message
	}
	return ""
}

func (m *EchoRequest) GetParam() *RequestParams {
	if m != nil {
		return m.Param
	}
	return nil
}

type ResponseParams struct {
	RequestDeadline      int64    `protobuf:"varint,1,opt,name=request_deadline,json=requestDeadline,proto3" json:"request_deadline,omitempty"`
	Host                 string   `protobuf:"bytes,2,opt,name=host,proto3" json:"host,omitempty"`
	Peer                 string   `protobuf:"bytes,3,opt,name=peer,proto3" json:"peer,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ResponseParams) Reset()         { *m = ResponseParams{} }
func (m *ResponseParams) String() string { return proto.CompactTextString(m) }
func (*ResponseParams) ProtoMessage()    {}
func (*ResponseParams) Descriptor() ([]byte, []int) {
	return fileDescriptor_700224c0fbd9d22b, []int{4}
}

func (m *ResponseParams) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ResponseParams.Unmarshal(m, b)
}
func (m *ResponseParams) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ResponseParams.Marshal(b, m, deterministic)
}
func (m *ResponseParams) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ResponseParams.Merge(m, src)
}
func (m *ResponseParams) XXX_Size() int {
	return xxx_messageInfo_ResponseParams.Size(m)
}
func (m *ResponseParams) XXX_DiscardUnknown() {
	xxx_messageInfo_ResponseParams.DiscardUnknown(m)
}

var xxx_messageInfo_ResponseParams proto.InternalMessageInfo

func (m *ResponseParams) GetRequestDeadline() int64 {
	if m != nil {
		return m.RequestDeadline
	}
	return 0
}

func (m *ResponseParams) GetHost() string {
	if m != nil {
		return m.Host
	}
	return ""
}

func (m *ResponseParams) GetPeer() string {
	if m != nil {
		return m.Peer
	}
	return ""
}

type EchoResponse struct {
	Message              string          `protobuf:"bytes,1,opt,name=message,proto3" json:"message,omitempty"`
	Param                *ResponseParams `protobuf:"bytes,2,opt,name=param,proto3" json:"param,omitempty"`
	XXX_NoUnkeyedLiteral struct{}        `json:"-"`
	XXX_unrecognized     []byte          `json:"-"`
	XXX_sizecache        int32           `json:"-"`
}

func (m *EchoResponse) Reset()         { *m = EchoResponse{} }
func (m *EchoResponse) String() string { return proto.CompactTextString(m) }
func (*EchoResponse) ProtoMessage()    {}
func (*EchoResponse) Descriptor() ([]byte, []int) {
	return fileDescriptor_700224c0fbd9d22b, []int{5}
}

func (m *EchoResponse) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_EchoResponse.Unmarshal(m, b)
}
func (m *EchoResponse) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_EchoResponse.Marshal(b, m, deterministic)
}
func (m *EchoResponse) XXX_Merge(src proto.Message) {
	xxx_messageInfo_EchoResponse.Merge(m, src)
}
func (m *EchoResponse) XXX_Size() int {
	return xxx_messageInfo_EchoResponse.Size(m)
}
func (m *EchoResponse) XXX_DiscardUnknown() {
	xxx_messageInfo_EchoResponse.DiscardUnknown(m)
}

var xxx_messageInfo_EchoResponse proto.InternalMessageInfo

func (m *EchoResponse) GetMessage() string {
	if m != nil {
		return m.Message
	}
	return ""
}

func (m *EchoResponse) GetParam() *ResponseParams {
	if m != nil {
		return m.Param
	}
	return nil
}

func init() {
	proto.RegisterType((*DebugInfo)(nil), "grpc.testing.DebugInfo")
	proto.RegisterType((*ErrorStatus)(nil), "grpc.testing.ErrorStatus")
	proto.RegisterType((*RequestParams)(nil), "grpc.testing.RequestParams")
	proto.RegisterType((*EchoRequest)(nil), "grpc.testing.EchoRequest")
	proto.RegisterType((*ResponseParams)(nil), "grpc.testing.ResponseParams")
	proto.RegisterType((*EchoResponse)(nil), "grpc.testing.EchoResponse")
}

func init() {
	proto.RegisterFile("src/proto/grpc/testing/echo_messages.proto", fileDescriptor_700224c0fbd9d22b)
}

var fileDescriptor_700224c0fbd9d22b = []byte{
	// 746 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x7c, 0x54, 0x61, 0x6f, 0x23, 0x35,
	0x10, 0xd5, 0x92, 0xa6, 0x6d, 0x9c, 0xa4, 0xed, 0xf9, 0xb8, 0x9e, 0xd1, 0x51, 0x29, 0x04, 0x09,
	0x05, 0x84, 0x12, 0xe8, 0x49, 0x27, 0x3e, 0x72, 0x24, 0x15, 0x8d, 0x44, 0x51, 0xb5, 0x69, 0x85,
	0x84, 0x90, 0x2c, 0xc7, 0x3b, 0xc9, 0x5a, 0xd9, 0x7a, 0x17, 0xdb, 0x81, 0xe4, 0xdf, 0xf0, 0x33,
	0xf9, 0x88, 0x3c, 0xf6, 0x86, 0x04, 0xd4, 0xfb, 0xb6, 0xfb, 0xde, 0xb3, 0xfd, 0x66, 0xfc, 0xc6,
	0xe4, 0x2b, 0x6b, 0xe4, 0xa8, 0x32, 0xa5, 0x2b, 0x47, 0x4b, 0x53, 0xc9, 0x91, 0x03, 0xeb, 0x94,
	0x5e, 0x8e, 0x40, 0xe6, 0x25, 0x7f, 0x02, 0x6b, 0xc5, 0x12, 0xec, 0x10, 0x05, 0xb4, 0xe3, 0x15,
	0xc3, 0xa8, 0xe8, 0xdf, 0x92, 0xd6, 0x04, 0xe6, 0xeb, 0xe5, 0x54, 0x2f, 0x4a, 0xfa, 0x39, 0xe9,
	0x5a, 0x27, 0xe4, 0x8a, 0x83, 0x76, 0x46, 0x81, 0x65, 0x49, 0xaf, 0x31, 0x68, 0xa5, 0x1d, 0x04,
	0x6f, 0x02, 0x46, 0x2f, 0xc9, 0x71, 0x06, 0x4e, 0xa8, 0x82, 0x7d, 0xd4, 0x4b, 0x06, 0xad, 0x34,
	0xfe, 0xf5, 0x37, 0xa4, 0x7d, 0x63, 0x4c, 0x69, 0x66, 0x4e, 0xb8, 0xb5, 0xa5, 0x94, 0x1c, 0xc9,
	0x32, 0x03, 0x96, 0xf4, 0x92, 0x41, 0x33, 0xc5, 0x6f, 0xbf, 0x3f, 0x78, 0x49, 0x6d, 0x29, 0xee,
	0xd0, 0x41, 0xf0, 0x2e, 0x60, 0xf4, 0x1b, 0xf2, 0xf1, 0x5c, 0x69, 0x61, 0xb6, 0x3c, 0x68, 0xc3,
	0xf6, 0x96, 0x35, 0x50, 0x4b, 0x03, 0x87, 0x27, 0x4d, 0x02, 0xd3, 0xff, 0xeb, 0x84, 0x74, 0x53,
	0xf8, 0x7d, 0x0d, 0xd6, 0xdd, 0x0b, 0x23, 0x9e, 0x2c, 0x1e, 0xe4, 0x4b, 0xcf, 0x40, 0x64, 0x85,
	0xd2, 0xc1, 0xc5, 0x69, 0xda, 0xf1, 0xe0, 0x24, 0x62, 0xf4, 0x2d, 0xb9, 0x94, 0x85, 0x02, 0xed,
	0xb8, 0x14, 0x5a, 0x42, 0xc1, 0xc5, 0xc2, 0x81, 0xe1, 0x6b, 0x8b, 0xb6, 0x9a, 0xe9, 0xcb, 0xc0,
	0x8e, 0x91, 0x7c, 0xef, 0xb9, 0x47, 0xeb, 0x17, 0x59, 0x30, 0x7f, 0x80, 0xf9, 0xdf, 0xa2, 0x46,
	0x58, 0x14, 0xd8, 0xc3, 0x45, 0xb5, 0x9d, 0x27, 0x70, 0x22, 0x13, 0x4e, 0xb0, 0xa3, 0x7f, 0xed,
	0xdc, 0x45, 0x8c, 0x7e, 0x4d, 0xa8, 0xcc, 0x41, 0xae, 0xb8, 0x58, 0xbb, 0x9c, 0xcb, 0x52, 0x3b,
	0xd8, 0x38, 0xd6, 0x44, 0xe5, 0x05, 0x32, 0xef, 0xd7, 0x2e, 0x1f, 0x07, 0x9c, 0xbe, 0x23, 0xaf,
	0x0d, 0xd8, 0xaa, 0xd4, 0x16, 0xea, 0x6e, 0xf2, 0x02, 0xf4, 0xd2, 0xe5, 0xec, 0x18, 0x8d, 0xbc,
	0xaa, 0xe9, 0xd8, 0xd7, 0x9f, 0x90, 0xa4, 0x6f, 0x48, 0x0b, 0xad, 0x54, 0x00, 0x86, 0x9d, 0xe0,
	0xe6, 0xa7, 0x1e, 0xb8, 0x07, 0x30, 0xf4, 0x3b, 0xc2, 0x60, 0x53, 0x81, 0x74, 0x90, 0xf1, 0xd8,
	0x1a, 0x95, 0x81, 0x76, 0xca, 0x6d, 0xd9, 0x29, 0xb6, 0xff, 0xb2, 0xe6, 0xc7, 0x48, 0x4f, 0x23,
	0xeb, 0x2f, 0xcd, 0xae, 0x54, 0x15, 0x9b, 0x52, 0xf8, 0xf5, 0xde, 0x31, 0x6b, 0xe1, 0x09, 0xd4,
	0x73, 0xe3, 0x9a, 0x1a, 0x7b, 0x86, 0xfe, 0x48, 0x7a, 0xbb, 0xb3, 0x9c, 0x11, 0xda, 0x56, 0xa5,
	0x71, 0xdc, 0x82, 0x5c, 0x1b, 0xe5, 0xb6, 0xdc, 0x6d, 0x2b, 0x60, 0x04, 0xcf, 0xbc, 0xaa, 0x75,
	0x0f, 0xb5, 0x6c, 0x16, 0x55, 0x0f, 0xdb, 0x0a, 0xe8, 0x3b, 0x42, 0x32, 0x9f, 0x60, 0xae, 0xf4,
	0xa2, 0x64, 0xed, 0x5e, 0x32, 0x68, 0x5f, 0xbf, 0x1e, 0xee, 0x87, 0x7c, 0xb8, 0x4b, 0x78, 0xda,
	0xca, 0x76, 0x61, 0xbf, 0x22, 0x24, 0xde, 0x64, 0xa6, 0x80, 0x75, 0xd0, 0x68, 0x2b, 0x20, 0x13,
	0xf5, 0x7c, 0x0c, 0xbb, 0xcf, 0xc5, 0x90, 0x7e, 0x4f, 0xce, 0x76, 0x15, 0xe1, 0x1a, 0x76, 0x86,
	0x66, 0x3e, 0x39, 0x34, 0xb3, 0x37, 0x24, 0x69, 0xb7, 0x5e, 0x80, 0x20, 0xfd, 0x82, 0x9c, 0x47,
	0x4b, 0xb6, 0x00, 0xa8, 0x7c, 0xaa, 0xce, 0xf1, 0x32, 0xbb, 0x01, 0x9e, 0x79, 0xf4, 0xd1, 0xd2,
	0x21, 0x79, 0x39, 0x17, 0x72, 0x05, 0xda, 0xb7, 0x59, 0x68, 0x0d, 0x05, 0x57, 0xd9, 0x86, 0x5d,
	0xa0, 0xf6, 0x45, 0xa4, 0xc6, 0x81, 0x99, 0x66, 0x1b, 0x1f, 0x96, 0x83, 0xfc, 0x71, 0xa5, 0x95,
	0x53, 0xa2, 0x28, 0xb6, 0xec, 0x05, 0xd6, 0xfd, 0x6a, 0x3f, 0x89, 0xd3, 0x9a, 0xa4, 0xb7, 0xe4,
	0xb3, 0xe8, 0x47, 0x97, 0x4e, 0x2d, 0xb6, 0x75, 0x28, 0xfe, 0xcc, 0x41, 0x73, 0xeb, 0x84, 0x71,
	0x90, 0x31, 0x8a, 0x3b, 0x5c, 0x05, 0xe1, 0xcf, 0xa8, 0x0b, 0xe1, 0xf8, 0x25, 0x07, 0x3d, 0x0b,
	0xa2, 0xfe, 0xaf, 0xa4, 0x7d, 0x23, 0xf3, 0x32, 0x4e, 0x29, 0x65, 0xe4, 0xa4, 0x7e, 0x02, 0x12,
	0xec, 0x67, 0xfd, 0x4b, 0xbf, 0x25, 0xcd, 0xca, 0xcf, 0x30, 0xce, 0x60, 0xfb, 0xfa, 0xcd, 0x61,
	0xef, 0x0e, 0xa6, 0x3c, 0x0d, 0xca, 0xbe, 0x24, 0x67, 0x69, 0xcc, 0x7a, 0x1c, 0xff, 0x2f, 0xc9,
	0x85, 0x09, 0xca, 0xc3, 0x17, 0xa0, 0x91, 0x9e, 0x47, 0x7c, 0xf7, 0x08, 0x50, 0x72, 0x94, 0x97,
	0xd6, 0xc5, 0x97, 0x08, 0xbf, 0x3d, 0x86, 0xe3, 0x11, 0x5e, 0x1c, 0xfc, 0xee, 0xff, 0x46, 0x3a,
	0xa1, 0x80, 0x70, 0xd0, 0x07, 0x2a, 0xb8, 0x3e, 0xac, 0xe0, 0xd3, 0xff, 0x56, 0xb0, 0xef, 0x34,
	0x96, 0xf0, 0x43, 0xe3, 0xef, 0x24, 0x99, 0x1f, 0xe3, 0xfb, 0xfc, 0xf6, 0x9f, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xf3, 0x96, 0x56, 0x6c, 0xcd, 0x05, 0x00, 0x00,
}
