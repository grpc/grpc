# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: envoy/extensions/filters/http/oauth2/v3/oauth.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from envoy.config.core.v3 import http_uri_pb2 as envoy_dot_config_dot_core_dot_v3_dot_http__uri__pb2
from envoy.config.route.v3 import route_components_pb2 as envoy_dot_config_dot_route_dot_v3_dot_route__components__pb2
from envoy.extensions.transport_sockets.tls.v3 import secret_pb2 as envoy_dot_extensions_dot_transport__sockets_dot_tls_dot_v3_dot_secret__pb2
from envoy.type.matcher.v3 import path_pb2 as envoy_dot_type_dot_matcher_dot_v3_dot_path__pb2
from google.protobuf import duration_pb2 as google_dot_protobuf_dot_duration__pb2
from google.protobuf import wrappers_pb2 as google_dot_protobuf_dot_wrappers__pb2
from udpa.annotations import status_pb2 as udpa_dot_annotations_dot_status__pb2
from validate import validate_pb2 as validate_dot_validate__pb2


DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n3envoy/extensions/filters/http/oauth2/v3/oauth.proto\x12\'envoy.extensions.filters.http.oauth2.v3\x1a#envoy/config/core/v3/http_uri.proto\x1a,envoy/config/route/v3/route_components.proto\x1a\x36\x65nvoy/extensions/transport_sockets/tls/v3/secret.proto\x1a envoy/type/matcher/v3/path.proto\x1a\x1egoogle/protobuf/duration.proto\x1a\x1egoogle/protobuf/wrappers.proto\x1a\x1dudpa/annotations/status.proto\x1a\x17validate/validate.proto\"\x99\x04\n\x11OAuth2Credentials\x12\x1a\n\tclient_id\x18\x01 \x01(\tB\x07\xfa\x42\x04r\x02\x10\x01\x12Z\n\x0ctoken_secret\x18\x02 \x01(\x0b\x32:.envoy.extensions.transport_sockets.tls.v3.SdsSecretConfigB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01\x12[\n\x0bhmac_secret\x18\x03 \x01(\x0b\x32:.envoy.extensions.transport_sockets.tls.v3.SdsSecretConfigB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01H\x00\x12\\\n\x0c\x63ookie_names\x18\x04 \x01(\x0b\x32\x46.envoy.extensions.filters.http.oauth2.v3.OAuth2Credentials.CookieNames\x1a\xb8\x01\n\x0b\x43ookieNames\x12!\n\x0c\x62\x65\x61rer_token\x18\x01 \x01(\tB\x0b\xfa\x42\x08r\x06\xc0\x01\x01\xd0\x01\x01\x12\x1f\n\noauth_hmac\x18\x02 \x01(\tB\x0b\xfa\x42\x08r\x06\xc0\x01\x01\xd0\x01\x01\x12\"\n\roauth_expires\x18\x03 \x01(\tB\x0b\xfa\x42\x08r\x06\xc0\x01\x01\xd0\x01\x01\x12\x1d\n\x08id_token\x18\x04 \x01(\tB\x0b\xfa\x42\x08r\x06\xc0\x01\x01\xd0\x01\x01\x12\"\n\rrefresh_token\x18\x05 \x01(\tB\x0b\xfa\x42\x08r\x06\xc0\x01\x01\xd0\x01\x01\x42\x16\n\x0ftoken_formation\x12\x03\xf8\x42\x01\"\x80\x06\n\x0cOAuth2Config\x12\x35\n\x0etoken_endpoint\x18\x01 \x01(\x0b\x32\x1d.envoy.config.core.v3.HttpUri\x12\'\n\x16\x61uthorization_endpoint\x18\x02 \x01(\tB\x07\xfa\x42\x04r\x02\x10\x01\x12Y\n\x0b\x63redentials\x18\x03 \x01(\x0b\x32:.envoy.extensions.filters.http.oauth2.v3.OAuth2CredentialsB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01\x12\x1d\n\x0credirect_uri\x18\x04 \x01(\tB\x07\xfa\x42\x04r\x02\x10\x01\x12K\n\x15redirect_path_matcher\x18\x05 \x01(\x0b\x32\".envoy.type.matcher.v3.PathMatcherB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01\x12\x42\n\x0csignout_path\x18\x06 \x01(\x0b\x32\".envoy.type.matcher.v3.PathMatcherB\x08\xfa\x42\x05\x8a\x01\x02\x10\x01\x12\x1c\n\x14\x66orward_bearer_token\x18\x07 \x01(\x08\x12\x42\n\x14pass_through_matcher\x18\x08 \x03(\x0b\x32$.envoy.config.route.v3.HeaderMatcher\x12\x13\n\x0b\x61uth_scopes\x18\t \x03(\t\x12\x11\n\tresources\x18\n \x03(\t\x12[\n\tauth_type\x18\x0b \x01(\x0e\x32>.envoy.extensions.filters.http.oauth2.v3.OAuth2Config.AuthTypeB\x08\xfa\x42\x05\x82\x01\x02\x10\x01\x12\x35\n\x11use_refresh_token\x18\x0c \x01(\x0b\x32\x1a.google.protobuf.BoolValue\x12\x35\n\x12\x64\x65\x66\x61ult_expires_in\x18\r \x01(\x0b\x32\x19.google.protobuf.Duration\"0\n\x08\x41uthType\x12\x14\n\x10URL_ENCODED_BODY\x10\x00\x12\x0e\n\nBASIC_AUTH\x10\x01\"O\n\x06OAuth2\x12\x45\n\x06\x63onfig\x18\x01 \x01(\x0b\x32\x35.envoy.extensions.filters.http.oauth2.v3.OAuth2ConfigB\xa6\x01\n5io.envoyproxy.envoy.extensions.filters.http.oauth2.v3B\nOauthProtoP\x01ZWgithub.com/envoyproxy/go-control-plane/envoy/extensions/filters/http/oauth2/v3;oauth2v3\xba\x80\xc8\xd1\x06\x02\x10\x02\x62\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'envoy.extensions.filters.http.oauth2.v3.oauth_pb2', _globals)
if _descriptor._USE_C_DESCRIPTORS == False:
  DESCRIPTOR._options = None
  DESCRIPTOR._serialized_options = b'\n5io.envoyproxy.envoy.extensions.filters.http.oauth2.v3B\nOauthProtoP\001ZWgithub.com/envoyproxy/go-control-plane/envoy/extensions/filters/http/oauth2/v3;oauth2v3\272\200\310\321\006\002\020\002'
  _OAUTH2CREDENTIALS_COOKIENAMES.fields_by_name['bearer_token']._options = None
  _OAUTH2CREDENTIALS_COOKIENAMES.fields_by_name['bearer_token']._serialized_options = b'\372B\010r\006\300\001\001\320\001\001'
  _OAUTH2CREDENTIALS_COOKIENAMES.fields_by_name['oauth_hmac']._options = None
  _OAUTH2CREDENTIALS_COOKIENAMES.fields_by_name['oauth_hmac']._serialized_options = b'\372B\010r\006\300\001\001\320\001\001'
  _OAUTH2CREDENTIALS_COOKIENAMES.fields_by_name['oauth_expires']._options = None
  _OAUTH2CREDENTIALS_COOKIENAMES.fields_by_name['oauth_expires']._serialized_options = b'\372B\010r\006\300\001\001\320\001\001'
  _OAUTH2CREDENTIALS_COOKIENAMES.fields_by_name['id_token']._options = None
  _OAUTH2CREDENTIALS_COOKIENAMES.fields_by_name['id_token']._serialized_options = b'\372B\010r\006\300\001\001\320\001\001'
  _OAUTH2CREDENTIALS_COOKIENAMES.fields_by_name['refresh_token']._options = None
  _OAUTH2CREDENTIALS_COOKIENAMES.fields_by_name['refresh_token']._serialized_options = b'\372B\010r\006\300\001\001\320\001\001'
  _OAUTH2CREDENTIALS.oneofs_by_name['token_formation']._options = None
  _OAUTH2CREDENTIALS.oneofs_by_name['token_formation']._serialized_options = b'\370B\001'
  _OAUTH2CREDENTIALS.fields_by_name['client_id']._options = None
  _OAUTH2CREDENTIALS.fields_by_name['client_id']._serialized_options = b'\372B\004r\002\020\001'
  _OAUTH2CREDENTIALS.fields_by_name['token_secret']._options = None
  _OAUTH2CREDENTIALS.fields_by_name['token_secret']._serialized_options = b'\372B\005\212\001\002\020\001'
  _OAUTH2CREDENTIALS.fields_by_name['hmac_secret']._options = None
  _OAUTH2CREDENTIALS.fields_by_name['hmac_secret']._serialized_options = b'\372B\005\212\001\002\020\001'
  _OAUTH2CONFIG.fields_by_name['authorization_endpoint']._options = None
  _OAUTH2CONFIG.fields_by_name['authorization_endpoint']._serialized_options = b'\372B\004r\002\020\001'
  _OAUTH2CONFIG.fields_by_name['credentials']._options = None
  _OAUTH2CONFIG.fields_by_name['credentials']._serialized_options = b'\372B\005\212\001\002\020\001'
  _OAUTH2CONFIG.fields_by_name['redirect_uri']._options = None
  _OAUTH2CONFIG.fields_by_name['redirect_uri']._serialized_options = b'\372B\004r\002\020\001'
  _OAUTH2CONFIG.fields_by_name['redirect_path_matcher']._options = None
  _OAUTH2CONFIG.fields_by_name['redirect_path_matcher']._serialized_options = b'\372B\005\212\001\002\020\001'
  _OAUTH2CONFIG.fields_by_name['signout_path']._options = None
  _OAUTH2CONFIG.fields_by_name['signout_path']._serialized_options = b'\372B\005\212\001\002\020\001'
  _OAUTH2CONFIG.fields_by_name['auth_type']._options = None
  _OAUTH2CONFIG.fields_by_name['auth_type']._serialized_options = b'\372B\005\202\001\002\020\001'
  _globals['_OAUTH2CREDENTIALS']._serialized_start=390
  _globals['_OAUTH2CREDENTIALS']._serialized_end=927
  _globals['_OAUTH2CREDENTIALS_COOKIENAMES']._serialized_start=719
  _globals['_OAUTH2CREDENTIALS_COOKIENAMES']._serialized_end=903
  _globals['_OAUTH2CONFIG']._serialized_start=930
  _globals['_OAUTH2CONFIG']._serialized_end=1698
  _globals['_OAUTH2CONFIG_AUTHTYPE']._serialized_start=1650
  _globals['_OAUTH2CONFIG_AUTHTYPE']._serialized_end=1698
  _globals['_OAUTH2']._serialized_start=1700
  _globals['_OAUTH2']._serialized_end=1779
# @@protoc_insertion_point(module_scope)
