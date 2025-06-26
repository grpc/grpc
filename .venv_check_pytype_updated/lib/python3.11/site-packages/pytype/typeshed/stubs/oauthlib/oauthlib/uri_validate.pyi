from typing import Any

DIGIT: str
ALPHA: str
HEXDIG: str
pct_encoded: Any
unreserved: Any
gen_delims: str
sub_delims: str
pchar: Any
reserved: Any
scheme: Any
dec_octet: Any
IPv4address: Any
IPv6address: str
IPvFuture: Any
IP_literal: Any
reg_name: Any
userinfo: Any
host: Any
port: Any
authority: Any
segment: Any
segment_nz: Any
segment_nz_nc: Any
path_abempty: Any
path_absolute: Any
path_noscheme: Any
path_rootless: Any
path_empty: str
path: Any
query: Any
fragment: Any
hier_part: Any
relative_part: Any
relative_ref: Any
URI: Any
URI_reference: Any
absolute_URI: Any

def is_uri(uri): ...
def is_uri_reference(uri): ...
def is_absolute_uri(uri): ...
