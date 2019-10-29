// Code generated by protoc-gen-go. DO NOT EDIT.
// source: extensions/stackdriver/edges/edges.proto

package google_cloud_meshtelemetry_v1alpha1

import (
	context "context"
	fmt "fmt"
	proto "github.com/golang/protobuf/proto"
	timestamp "github.com/golang/protobuf/ptypes/timestamp"
	grpc "google.golang.org/grpc"
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

type TrafficAssertion_Protocol int32

const (
	TrafficAssertion_PROTOCOL_UNSPECIFIED TrafficAssertion_Protocol = 0
	TrafficAssertion_PROTOCOL_HTTP        TrafficAssertion_Protocol = 1
	TrafficAssertion_PROTOCOL_HTTPS       TrafficAssertion_Protocol = 2
	TrafficAssertion_PROTOCOL_TCP         TrafficAssertion_Protocol = 3
	TrafficAssertion_PROTOCOL_GRPC        TrafficAssertion_Protocol = 4
)

var TrafficAssertion_Protocol_name = map[int32]string{
	0: "PROTOCOL_UNSPECIFIED",
	1: "PROTOCOL_HTTP",
	2: "PROTOCOL_HTTPS",
	3: "PROTOCOL_TCP",
	4: "PROTOCOL_GRPC",
}

var TrafficAssertion_Protocol_value = map[string]int32{
	"PROTOCOL_UNSPECIFIED": 0,
	"PROTOCOL_HTTP":        1,
	"PROTOCOL_HTTPS":       2,
	"PROTOCOL_TCP":         3,
	"PROTOCOL_GRPC":        4,
}

func (x TrafficAssertion_Protocol) String() string {
	return proto.EnumName(TrafficAssertion_Protocol_name, int32(x))
}

func (TrafficAssertion_Protocol) EnumDescriptor() ([]byte, []int) {
	return fileDescriptor_0b9d48fc1143c9cf, []int{3, 0}
}

type ReportTrafficAssertionsRequest struct {
	Parent               string               `protobuf:"bytes,1,opt,name=parent,proto3" json:"parent,omitempty"`
	MeshUid              string               `protobuf:"bytes,2,opt,name=mesh_uid,json=meshUid,proto3" json:"mesh_uid,omitempty"`
	TrafficAssertions    []*TrafficAssertion  `protobuf:"bytes,3,rep,name=traffic_assertions,json=trafficAssertions,proto3" json:"traffic_assertions,omitempty"`
	Timestamp            *timestamp.Timestamp `protobuf:"bytes,4,opt,name=timestamp,proto3" json:"timestamp,omitempty"`
	XXX_NoUnkeyedLiteral struct{}             `json:"-"`
	XXX_unrecognized     []byte               `json:"-"`
	XXX_sizecache        int32                `json:"-"`
}

func (m *ReportTrafficAssertionsRequest) Reset()         { *m = ReportTrafficAssertionsRequest{} }
func (m *ReportTrafficAssertionsRequest) String() string { return proto.CompactTextString(m) }
func (*ReportTrafficAssertionsRequest) ProtoMessage()    {}
func (*ReportTrafficAssertionsRequest) Descriptor() ([]byte, []int) {
	return fileDescriptor_0b9d48fc1143c9cf, []int{0}
}

func (m *ReportTrafficAssertionsRequest) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ReportTrafficAssertionsRequest.Unmarshal(m, b)
}
func (m *ReportTrafficAssertionsRequest) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ReportTrafficAssertionsRequest.Marshal(b, m, deterministic)
}
func (m *ReportTrafficAssertionsRequest) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ReportTrafficAssertionsRequest.Merge(m, src)
}
func (m *ReportTrafficAssertionsRequest) XXX_Size() int {
	return xxx_messageInfo_ReportTrafficAssertionsRequest.Size(m)
}
func (m *ReportTrafficAssertionsRequest) XXX_DiscardUnknown() {
	xxx_messageInfo_ReportTrafficAssertionsRequest.DiscardUnknown(m)
}

var xxx_messageInfo_ReportTrafficAssertionsRequest proto.InternalMessageInfo

func (m *ReportTrafficAssertionsRequest) GetParent() string {
	if m != nil {
		return m.Parent
	}
	return ""
}

func (m *ReportTrafficAssertionsRequest) GetMeshUid() string {
	if m != nil {
		return m.MeshUid
	}
	return ""
}

func (m *ReportTrafficAssertionsRequest) GetTrafficAssertions() []*TrafficAssertion {
	if m != nil {
		return m.TrafficAssertions
	}
	return nil
}

func (m *ReportTrafficAssertionsRequest) GetTimestamp() *timestamp.Timestamp {
	if m != nil {
		return m.Timestamp
	}
	return nil
}

type ReportTrafficAssertionsResponse struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ReportTrafficAssertionsResponse) Reset()         { *m = ReportTrafficAssertionsResponse{} }
func (m *ReportTrafficAssertionsResponse) String() string { return proto.CompactTextString(m) }
func (*ReportTrafficAssertionsResponse) ProtoMessage()    {}
func (*ReportTrafficAssertionsResponse) Descriptor() ([]byte, []int) {
	return fileDescriptor_0b9d48fc1143c9cf, []int{1}
}

func (m *ReportTrafficAssertionsResponse) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ReportTrafficAssertionsResponse.Unmarshal(m, b)
}
func (m *ReportTrafficAssertionsResponse) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ReportTrafficAssertionsResponse.Marshal(b, m, deterministic)
}
func (m *ReportTrafficAssertionsResponse) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ReportTrafficAssertionsResponse.Merge(m, src)
}
func (m *ReportTrafficAssertionsResponse) XXX_Size() int {
	return xxx_messageInfo_ReportTrafficAssertionsResponse.Size(m)
}
func (m *ReportTrafficAssertionsResponse) XXX_DiscardUnknown() {
	xxx_messageInfo_ReportTrafficAssertionsResponse.DiscardUnknown(m)
}

var xxx_messageInfo_ReportTrafficAssertionsResponse proto.InternalMessageInfo

type WorkloadInstance struct {
	Uid                  string   `protobuf:"bytes,1,opt,name=uid,proto3" json:"uid,omitempty"`
	Location             string   `protobuf:"bytes,2,opt,name=location,proto3" json:"location,omitempty"`
	ClusterName          string   `protobuf:"bytes,3,opt,name=cluster_name,json=clusterName,proto3" json:"cluster_name,omitempty"`
	OwnerUid             string   `protobuf:"bytes,4,opt,name=owner_uid,json=ownerUid,proto3" json:"owner_uid,omitempty"`
	WorkloadName         string   `protobuf:"bytes,5,opt,name=workload_name,json=workloadName,proto3" json:"workload_name,omitempty"`
	WorkloadNamespace    string   `protobuf:"bytes,6,opt,name=workload_namespace,json=workloadNamespace,proto3" json:"workload_namespace,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *WorkloadInstance) Reset()         { *m = WorkloadInstance{} }
func (m *WorkloadInstance) String() string { return proto.CompactTextString(m) }
func (*WorkloadInstance) ProtoMessage()    {}
func (*WorkloadInstance) Descriptor() ([]byte, []int) {
	return fileDescriptor_0b9d48fc1143c9cf, []int{2}
}

func (m *WorkloadInstance) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_WorkloadInstance.Unmarshal(m, b)
}
func (m *WorkloadInstance) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_WorkloadInstance.Marshal(b, m, deterministic)
}
func (m *WorkloadInstance) XXX_Merge(src proto.Message) {
	xxx_messageInfo_WorkloadInstance.Merge(m, src)
}
func (m *WorkloadInstance) XXX_Size() int {
	return xxx_messageInfo_WorkloadInstance.Size(m)
}
func (m *WorkloadInstance) XXX_DiscardUnknown() {
	xxx_messageInfo_WorkloadInstance.DiscardUnknown(m)
}

var xxx_messageInfo_WorkloadInstance proto.InternalMessageInfo

func (m *WorkloadInstance) GetUid() string {
	if m != nil {
		return m.Uid
	}
	return ""
}

func (m *WorkloadInstance) GetLocation() string {
	if m != nil {
		return m.Location
	}
	return ""
}

func (m *WorkloadInstance) GetClusterName() string {
	if m != nil {
		return m.ClusterName
	}
	return ""
}

func (m *WorkloadInstance) GetOwnerUid() string {
	if m != nil {
		return m.OwnerUid
	}
	return ""
}

func (m *WorkloadInstance) GetWorkloadName() string {
	if m != nil {
		return m.WorkloadName
	}
	return ""
}

func (m *WorkloadInstance) GetWorkloadNamespace() string {
	if m != nil {
		return m.WorkloadNamespace
	}
	return ""
}

type TrafficAssertion struct {
	Source                      *WorkloadInstance         `protobuf:"bytes,1,opt,name=source,proto3" json:"source,omitempty"`
	Destination                 *WorkloadInstance         `protobuf:"bytes,2,opt,name=destination,proto3" json:"destination,omitempty"`
	Protocol                    TrafficAssertion_Protocol `protobuf:"varint,3,opt,name=protocol,proto3,enum=google.cloud.meshtelemetry.v1alpha1.TrafficAssertion_Protocol" json:"protocol,omitempty"`
	DestinationServiceName      string                    `protobuf:"bytes,4,opt,name=destination_service_name,json=destinationServiceName,proto3" json:"destination_service_name,omitempty"`
	DestinationServiceNamespace string                    `protobuf:"bytes,5,opt,name=destination_service_namespace,json=destinationServiceNamespace,proto3" json:"destination_service_namespace,omitempty"`
	XXX_NoUnkeyedLiteral        struct{}                  `json:"-"`
	XXX_unrecognized            []byte                    `json:"-"`
	XXX_sizecache               int32                     `json:"-"`
}

func (m *TrafficAssertion) Reset()         { *m = TrafficAssertion{} }
func (m *TrafficAssertion) String() string { return proto.CompactTextString(m) }
func (*TrafficAssertion) ProtoMessage()    {}
func (*TrafficAssertion) Descriptor() ([]byte, []int) {
	return fileDescriptor_0b9d48fc1143c9cf, []int{3}
}

func (m *TrafficAssertion) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_TrafficAssertion.Unmarshal(m, b)
}
func (m *TrafficAssertion) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_TrafficAssertion.Marshal(b, m, deterministic)
}
func (m *TrafficAssertion) XXX_Merge(src proto.Message) {
	xxx_messageInfo_TrafficAssertion.Merge(m, src)
}
func (m *TrafficAssertion) XXX_Size() int {
	return xxx_messageInfo_TrafficAssertion.Size(m)
}
func (m *TrafficAssertion) XXX_DiscardUnknown() {
	xxx_messageInfo_TrafficAssertion.DiscardUnknown(m)
}

var xxx_messageInfo_TrafficAssertion proto.InternalMessageInfo

func (m *TrafficAssertion) GetSource() *WorkloadInstance {
	if m != nil {
		return m.Source
	}
	return nil
}

func (m *TrafficAssertion) GetDestination() *WorkloadInstance {
	if m != nil {
		return m.Destination
	}
	return nil
}

func (m *TrafficAssertion) GetProtocol() TrafficAssertion_Protocol {
	if m != nil {
		return m.Protocol
	}
	return TrafficAssertion_PROTOCOL_UNSPECIFIED
}

func (m *TrafficAssertion) GetDestinationServiceName() string {
	if m != nil {
		return m.DestinationServiceName
	}
	return ""
}

func (m *TrafficAssertion) GetDestinationServiceNamespace() string {
	if m != nil {
		return m.DestinationServiceNamespace
	}
	return ""
}

func init() {
	proto.RegisterEnum("google.cloud.meshtelemetry.v1alpha1.TrafficAssertion_Protocol", TrafficAssertion_Protocol_name, TrafficAssertion_Protocol_value)
	proto.RegisterType((*ReportTrafficAssertionsRequest)(nil), "google.cloud.meshtelemetry.v1alpha1.ReportTrafficAssertionsRequest")
	proto.RegisterType((*ReportTrafficAssertionsResponse)(nil), "google.cloud.meshtelemetry.v1alpha1.ReportTrafficAssertionsResponse")
	proto.RegisterType((*WorkloadInstance)(nil), "google.cloud.meshtelemetry.v1alpha1.WorkloadInstance")
	proto.RegisterType((*TrafficAssertion)(nil), "google.cloud.meshtelemetry.v1alpha1.TrafficAssertion")
}

func init() {
	proto.RegisterFile("extensions/stackdriver/edges/edges.proto", fileDescriptor_0b9d48fc1143c9cf)
}

var fileDescriptor_0b9d48fc1143c9cf = []byte{
	// 574 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xa4, 0x54, 0x51, 0x6e, 0xd3, 0x40,
	0x10, 0xad, 0x9b, 0x10, 0xd2, 0x49, 0x5b, 0xb9, 0x2b, 0x54, 0x4c, 0x2a, 0x68, 0x9b, 0xfe, 0xe4,
	0x07, 0x47, 0x0d, 0x42, 0xea, 0x17, 0x12, 0xa4, 0x05, 0x2a, 0xd1, 0xd6, 0x72, 0x5d, 0x55, 0xe2,
	0x27, 0xda, 0xda, 0x93, 0xc4, 0xaa, 0xed, 0x35, 0xbb, 0xeb, 0x14, 0x2e, 0xc0, 0x51, 0xb8, 0x04,
	0xd7, 0xe0, 0x26, 0x5c, 0x00, 0x79, 0xd7, 0x71, 0x93, 0x88, 0xa0, 0xa8, 0xfc, 0x44, 0xd9, 0x99,
	0x37, 0x6f, 0x66, 0xde, 0xf3, 0x2e, 0xb4, 0xf1, 0xab, 0xc4, 0x44, 0x84, 0x2c, 0x11, 0x1d, 0x21,
	0xa9, 0x7f, 0x1b, 0xf0, 0x70, 0x8c, 0xbc, 0x83, 0xc1, 0x10, 0x85, 0xfe, 0xb5, 0x53, 0xce, 0x24,
	0x23, 0x07, 0x43, 0xc6, 0x86, 0x11, 0xda, 0x7e, 0xc4, 0xb2, 0xc0, 0x8e, 0x51, 0x8c, 0x24, 0x46,
	0x18, 0xa3, 0xe4, 0xdf, 0xec, 0xf1, 0x21, 0x8d, 0xd2, 0x11, 0x3d, 0x6c, 0xee, 0x6a, 0x50, 0x47,
	0x95, 0xdc, 0x64, 0x83, 0x8e, 0x0c, 0x63, 0x14, 0x92, 0xc6, 0xa9, 0x66, 0x69, 0xfd, 0x36, 0xe0,
	0x85, 0x8b, 0x29, 0xe3, 0xd2, 0xe3, 0x74, 0x30, 0x08, 0xfd, 0xb7, 0x42, 0x20, 0x97, 0x79, 0x7f,
	0x17, 0xbf, 0x64, 0x28, 0x24, 0xd9, 0x86, 0x5a, 0x4a, 0x39, 0x26, 0xd2, 0x32, 0xf6, 0x8c, 0xf6,
	0x9a, 0x5b, 0x9c, 0xc8, 0x33, 0xa8, 0xe7, 0x5d, 0xfb, 0x59, 0x18, 0x58, 0xab, 0x2a, 0xf3, 0x38,
	0x3f, 0x5f, 0x85, 0x01, 0x09, 0x80, 0x48, 0x4d, 0xd7, 0xa7, 0x25, 0x9f, 0x55, 0xd9, 0xab, 0xb4,
	0x1b, 0xdd, 0xd7, 0xf6, 0x12, 0x83, 0xdb, 0xf3, 0xd3, 0xb8, 0x5b, 0x72, 0x7e, 0x3e, 0x72, 0x04,
	0x6b, 0xe5, 0x3a, 0x56, 0x75, 0xcf, 0x68, 0x37, 0xba, 0xcd, 0x09, 0xf9, 0x64, 0x61, 0xdb, 0x9b,
	0x20, 0xdc, 0x7b, 0x70, 0x6b, 0x1f, 0x76, 0x17, 0x2e, 0x2d, 0x52, 0x96, 0x08, 0x6c, 0xfd, 0x32,
	0xc0, 0xbc, 0x66, 0xfc, 0x36, 0x62, 0x34, 0x38, 0x4d, 0x84, 0xa4, 0x89, 0x8f, 0xc4, 0x84, 0x4a,
	0xbe, 0xad, 0xd6, 0x21, 0xff, 0x4b, 0x9a, 0x50, 0x8f, 0x98, 0x4f, 0xf3, 0xda, 0x42, 0x84, 0xf2,
	0x4c, 0xf6, 0x61, 0xdd, 0x8f, 0x32, 0x21, 0x91, 0xf7, 0x13, 0x1a, 0xa3, 0x55, 0x51, 0xf9, 0x46,
	0x11, 0x3b, 0xa7, 0x31, 0x92, 0x1d, 0x58, 0x63, 0x77, 0x09, 0x72, 0x25, 0x62, 0x55, 0xd7, 0xab,
	0x40, 0xae, 0xe2, 0x01, 0x6c, 0xdc, 0x15, 0x13, 0x68, 0x82, 0x47, 0x0a, 0xb0, 0x3e, 0x09, 0x2a,
	0x86, 0x97, 0x40, 0x66, 0x40, 0x22, 0xa5, 0x3e, 0x5a, 0x35, 0x85, 0xdc, 0x9a, 0x46, 0xaa, 0x44,
	0xeb, 0x7b, 0x15, 0xcc, 0xf9, 0xa5, 0xc9, 0x19, 0xd4, 0x04, 0xcb, 0xb8, 0x8f, 0x6a, 0xb3, 0x65,
	0x2d, 0x9a, 0x57, 0xc7, 0x2d, 0x48, 0xc8, 0x35, 0x34, 0x02, 0x14, 0x32, 0x4c, 0xee, 0x65, 0x79,
	0x30, 0xe7, 0x34, 0x13, 0xf9, 0x0c, 0x75, 0xe5, 0xab, 0xcf, 0x22, 0x25, 0xe6, 0x66, 0xf7, 0xcd,
	0x83, 0x3e, 0x26, 0xdb, 0x29, 0x58, 0xdc, 0x92, 0x8f, 0x1c, 0x81, 0x35, 0xd5, 0xaa, 0x2f, 0x90,
	0x8f, 0x43, 0x1f, 0xb5, 0xee, 0xda, 0x98, 0xed, 0xa9, 0xfc, 0xa5, 0x4e, 0x2b, 0x07, 0xde, 0xc1,
	0xf3, 0x45, 0x95, 0xda, 0x0c, 0x6d, 0xdb, 0xce, 0xdf, 0xcb, 0xb5, 0x2d, 0x29, 0xd4, 0x27, 0x33,
	0x11, 0x0b, 0x9e, 0x38, 0xee, 0x85, 0x77, 0xd1, 0xbb, 0xf8, 0xd4, 0xbf, 0x3a, 0xbf, 0x74, 0x4e,
	0x7a, 0xa7, 0xef, 0x4f, 0x4f, 0x8e, 0xcd, 0x15, 0xb2, 0x05, 0x1b, 0x65, 0xe6, 0xa3, 0xe7, 0x39,
	0xa6, 0x41, 0x08, 0x6c, 0xce, 0x84, 0x2e, 0xcd, 0x55, 0x62, 0xc2, 0x7a, 0x19, 0xf3, 0x7a, 0x8e,
	0x59, 0x99, 0x29, 0xfc, 0xe0, 0x3a, 0x3d, 0xb3, 0xda, 0xfd, 0x69, 0x80, 0x79, 0x86, 0x62, 0x74,
	0x92, 0x3f, 0x29, 0xc5, 0x3c, 0xe4, 0x87, 0x01, 0x4f, 0x17, 0x5c, 0x0c, 0xd2, 0x5b, 0x4a, 0xea,
	0x7f, 0xbf, 0x25, 0xcd, 0xe3, 0xff, 0x23, 0x29, 0xee, 0xe6, 0xca, 0x4d, 0x4d, 0xf9, 0xf6, 0xea,
	0x4f, 0x00, 0x00, 0x00, 0xff, 0xff, 0xf0, 0xe1, 0x42, 0x30, 0x2f, 0x05, 0x00, 0x00,
}

// Reference imports to suppress errors if they are not otherwise used.
var _ context.Context
var _ grpc.ClientConn

// This is a compile-time assertion to ensure that this generated file
// is compatible with the grpc package it is being compiled against.
const _ = grpc.SupportPackageIsVersion4

// MeshEdgesServiceClient is the client API for MeshEdgesService service.
//
// For semantics around ctx use and closing/ending streaming RPCs, please refer to https://godoc.org/google.golang.org/grpc#ClientConn.NewStream.
type MeshEdgesServiceClient interface {
	ReportTrafficAssertions(ctx context.Context, in *ReportTrafficAssertionsRequest, opts ...grpc.CallOption) (*ReportTrafficAssertionsResponse, error)
}

type meshEdgesServiceClient struct {
	cc *grpc.ClientConn
}

func NewMeshEdgesServiceClient(cc *grpc.ClientConn) MeshEdgesServiceClient {
	return &meshEdgesServiceClient{cc}
}

func (c *meshEdgesServiceClient) ReportTrafficAssertions(ctx context.Context, in *ReportTrafficAssertionsRequest, opts ...grpc.CallOption) (*ReportTrafficAssertionsResponse, error) {
	out := new(ReportTrafficAssertionsResponse)
	err := c.cc.Invoke(ctx, "/google.cloud.meshtelemetry.v1alpha1.MeshEdgesService/ReportTrafficAssertions", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

// MeshEdgesServiceServer is the server API for MeshEdgesService service.
type MeshEdgesServiceServer interface {
	ReportTrafficAssertions(context.Context, *ReportTrafficAssertionsRequest) (*ReportTrafficAssertionsResponse, error)
}

func RegisterMeshEdgesServiceServer(s *grpc.Server, srv MeshEdgesServiceServer) {
	s.RegisterService(&_MeshEdgesService_serviceDesc, srv)
}

func _MeshEdgesService_ReportTrafficAssertions_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(ReportTrafficAssertionsRequest)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MeshEdgesServiceServer).ReportTrafficAssertions(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/google.cloud.meshtelemetry.v1alpha1.MeshEdgesService/ReportTrafficAssertions",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MeshEdgesServiceServer).ReportTrafficAssertions(ctx, req.(*ReportTrafficAssertionsRequest))
	}
	return interceptor(ctx, in, info, handler)
}

var _MeshEdgesService_serviceDesc = grpc.ServiceDesc{
	ServiceName: "google.cloud.meshtelemetry.v1alpha1.MeshEdgesService",
	HandlerType: (*MeshEdgesServiceServer)(nil),
	Methods: []grpc.MethodDesc{
		{
			MethodName: "ReportTrafficAssertions",
			Handler:    _MeshEdgesService_ReportTrafficAssertions_Handler,
		},
	},
	Streams:  []grpc.StreamDesc{},
	Metadata: "extensions/stackdriver/edges/edges.proto",
}
