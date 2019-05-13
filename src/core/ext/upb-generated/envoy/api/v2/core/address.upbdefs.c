/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/api/v2/core/address.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"

extern upb_def_init envoy_api_v2_core_base_proto_upbdefinit;
extern upb_def_init google_protobuf_wrappers_proto_upbdefinit;
extern upb_def_init validate_validate_proto_upbdefinit;
extern upb_def_init gogoproto_gogo_proto_upbdefinit;
static const char descriptor[1368] =
"\n\037envoy/api/v2/core/address.proto\022\021envoy"
".api.v2.core\032\034envoy/api/v2/core/base.pro"
"to\032\036google/protobuf/wrappers.proto\032\027vali"
"date/validate.proto\032\024gogoproto/gogo.prot"
"o\"%\n\004Pipe\022\035\n\004path\030\001 \001(\tB\t\272\351\300\003\004r\002 \001R\004path"
"\"\331\002\n\rSocketAddress\022Q\n\010protocol\030\001 \001(\0162).e"
"nvoy.api.v2.core.SocketAddress.ProtocolB"
"\n\272\351\300\003\005\202\001\002\020\001R\010protocol\022#\n\007address\030\002 \001(\tB\t"
"\272\351\300\003\004r\002 \001R\007address\022,\n\nport_value\030\003 \001(\rB\013"
"\272\351\300\003\006*\004\030\377\377\003H\000R\tportValue\022\037\n\nnamed_port\030\004"
" \001(\tH\000R\tnamedPort\022#\n\rresolver_name\030\005 \001(\t"
"R\014resolverName\022\037\n\013ipv4_compat\030\006 \001(\010R\nipv"
"4Compat\"\"\n\010Protocol\022\007\n\003TCP\020\000\022\007\n\003UDP\020\001\032\004\210"
"\243\036\000B\027\n\016port_specifier\022\005\270\351\300\003\001\"\351\001\n\014TcpKeep"
"alive\022G\n\020keepalive_probes\030\001 \001(\0132\034.google"
".protobuf.UInt32ValueR\017keepaliveProbes\022C"
"\n\016keepalive_time\030\002 \001(\0132\034.google.protobuf"
".UInt32ValueR\rkeepaliveTime\022K\n\022keepalive"
"_interval\030\003 \001(\0132\034.google.protobuf.UInt32"
"ValueR\021keepaliveInterval\"\345\001\n\nBindConfig\022"
"W\n\016source_address\030\001 \001(\0132 .envoy.api.v2.c"
"ore.SocketAddressB\016\272\351\300\003\005\212\001\002\020\001\310\336\037\000R\rsourc"
"eAddress\0226\n\010freebind\030\002 \001(\0132\032.google.prot"
"obuf.BoolValueR\010freebind\022F\n\016socket_optio"
"ns\030\003 \003(\0132\037.envoy.api.v2.core.SocketOptio"
"nR\rsocketOptions\"\225\001\n\007Address\022I\n\016socket_a"
"ddress\030\001 \001(\0132 .envoy.api.v2.core.SocketA"
"ddressH\000R\rsocketAddress\022-\n\004pipe\030\002 \001(\0132\027."
"envoy.api.v2.core.PipeH\000R\004pipeB\020\n\007addres"
"s\022\005\270\351\300\003\001\"\206\001\n\tCidrRange\0220\n\016address_prefix"
"\030\001 \001(\tB\t\272\351\300\003\004r\002 \001R\raddressPrefix\022G\n\npref"
"ix_len\030\002 \001(\0132\034.google.protobuf.UInt32Val"
"ueB\n\272\351\300\003\005*\003\030\200\001R\tprefixLenB5\n\037io.envoypro"
"xy.envoy.api.v2.coreB\014AddressProtoP\001\250\342\036\001"
"b\006proto3"
;
static upb_def_init *deps[5] = {
  &envoy_api_v2_core_base_proto_upbdefinit,
  &google_protobuf_wrappers_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  &gogoproto_gogo_proto_upbdefinit,
  NULL
};
upb_def_init envoy_api_v2_core_address_proto_upbdefinit = {
  deps,
  "envoy/api/v2/core/address.proto",
  UPB_STRVIEW_INIT(descriptor, 1368)
};
