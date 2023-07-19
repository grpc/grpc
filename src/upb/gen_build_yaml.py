#!/usr/bin/env python2.7

# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# TODO: This should ideally be in upb submodule to avoid hardcoding this here.

import re
import os
import sys
import yaml

out = {}

try:
    out["libs"] = [
        {
            "name": "upb",
            "build": "all",
            "language": "c",
            "src": [
                "third_party/utf8_range/naive.c",
                "third_party/utf8_range/range2-neon.c",
                "third_party/utf8_range/range2-sse.c",
                "third_party/upb/upb/base/status.c",
                "third_party/upb/upb/collections/array.c",
                "third_party/upb/upb/collections/map.c",
                "third_party/upb/upb/collections/map_sorter.c",
                "third_party/upb/upb/hash/common.c",
                "third_party/upb/upb/lex/atoi.c",
                "third_party/upb/upb/lex/round_trip.c",
                "third_party/upb/upb/lex/strtod.c",
                "third_party/upb/upb/lex/unicode.c",
                "third_party/upb/upb/mem/alloc.c",
                "third_party/upb/upb/mem/arena.c",
                "third_party/upb/upb/message/accessors.c",
                "third_party/upb/upb/message/message.c",
                "third_party/upb/upb/reflection/def_builder.c",
                "third_party/upb/upb/reflection/def_pool.c",
                "third_party/upb/upb/reflection/def_type.c",
                "third_party/upb/upb/reflection/desc_state.c",
                "third_party/upb/upb/reflection/enum_def.c",
                "third_party/upb/upb/reflection/enum_reserved_range.c",
                "third_party/upb/upb/reflection/enum_value_def.c",
                "third_party/upb/upb/reflection/extension_range.c",
                "third_party/upb/upb/reflection/field_def.c",
                "third_party/upb/upb/reflection/file_def.c",
                "third_party/upb/upb/reflection/message.c",
                "third_party/upb/upb/reflection/message_def.c",
                "third_party/upb/upb/reflection/message_reserved_range.c",
                "third_party/upb/upb/reflection/method_def.c",
                "third_party/upb/upb/reflection/oneof_def.c",
                "third_party/upb/upb/reflection/service_def.c",
                "third_party/upb/upb/text/encode.c",
                "third_party/upb/upb/wire/decode.c",
                "third_party/upb/upb/wire/decode_fast.c",
                "third_party/upb/upb/wire/encode.c",
                "third_party/upb/upb/wire/eps_copy_input_stream.c",
                "third_party/upb/upb/wire/reader.c",
                "src/core/ext/upb-generated/google/protobuf/descriptor.upb.c",
                "src/core/ext/upbdefs-generated/google/protobuf/descriptor.upbdefs.c",
            ],
            "headers": [
                "third_party/utf8_range/utf8_range.h",
                "third_party/upb/upb/alloc.h",
                "third_party/upb/upb/arena.h",
                "third_party/upb/upb/array.h",
                "third_party/upb/upb/base/descriptor_constants.h",
                "third_party/upb/upb/base/log2.h",
                "third_party/upb/upb/base/status.h",
                "third_party/upb/upb/base/string_view.h",
                "third_party/upb/upb/collections/array.h",
                "third_party/upb/upb/collections/array_internal.h",
                "third_party/upb/upb/collections/map_gencode_util.h",
                "third_party/upb/upb/collections/map.h",
                "third_party/upb/upb/collections/map_internal.h",
                "third_party/upb/upb/collections/map_sorter_internal.h",
                "third_party/upb/upb/collections/message_value.h",
                "third_party/upb/upb/decode.h",
                "third_party/upb/upb/def.h",
                "third_party/upb/upb/def.hpp",
                "third_party/upb/upb/encode.h",
                "third_party/upb/upb/extension_registry.h",
                "third_party/upb/upb/generated_code_support.h",
                "third_party/upb/upb/hash/common.h",
                "third_party/upb/upb/hash/int_table.h",
                "third_party/upb/upb/hash/str_table.h",
                "third_party/upb/upb/lex/atoi.h",
                "third_party/upb/upb/lex/round_trip.h",
                "third_party/upb/upb/lex/strtod.h",
                "third_party/upb/upb/lex/unicode.h",
                "third_party/upb/upb/map.h",
                "third_party/upb/upb/mem/alloc.h",
                "third_party/upb/upb/mem/arena.h",
                "third_party/upb/upb/mem/arena_internal.h",
                "third_party/upb/upb/message/accessors.h",
                "third_party/upb/upb/message/accessors_internal.h",
                "third_party/upb/upb/message/extension_internal.h",
                "third_party/upb/upb/message/internal.h",
                "third_party/upb/upb/message/internal/map_entry.h",
                "third_party/upb/upb/message/message.h",
                "third_party/upb/upb/message/tagged_ptr.h",
                "third_party/upb/upb/msg.h",
                "third_party/upb/upb/port/atomic.h",
                "third_party/upb/upb/port/def.inc",
                "third_party/upb/upb/port/undef.inc",
                "third_party/upb/upb/port/vsnprintf_compat.h",
                "third_party/upb/upb/reflection/common.h",
                "third_party/upb/upb/reflection/def_builder_internal.h",
                "third_party/upb/upb/reflection/def.h",
                "third_party/upb/upb/reflection/def.hpp",
                "third_party/upb/upb/reflection/def_pool.h",
                "third_party/upb/upb/reflection/def_pool_internal.h",
                "third_party/upb/upb/reflection/def_type.h",
                "third_party/upb/upb/reflection/desc_state_internal.h",
                "third_party/upb/upb/reflection/enum_def.h",
                "third_party/upb/upb/reflection/enum_def_internal.h",
                "third_party/upb/upb/reflection/enum_reserved_range.h",
                "third_party/upb/upb/reflection/enum_reserved_range_internal.h",
                "third_party/upb/upb/reflection/enum_value_def.h",
                "third_party/upb/upb/reflection/enum_value_def_internal.h",
                "third_party/upb/upb/reflection/extension_range.h",
                "third_party/upb/upb/reflection/extension_range_internal.h",
                "third_party/upb/upb/reflection/field_def.h",
                "third_party/upb/upb/reflection/field_def_internal.h",
                "third_party/upb/upb/reflection/file_def.h",
                "third_party/upb/upb/reflection/file_def_internal.h",
                "third_party/upb/upb/reflection.h",
                "third_party/upb/upb/reflection.hpp",
                "third_party/upb/upb/reflection/message_def.h",
                "third_party/upb/upb/reflection/message_def_internal.h",
                "third_party/upb/upb/reflection/message.h",
                "third_party/upb/upb/reflection/message.hpp",
                "third_party/upb/upb/reflection/message_reserved_range.h",
                "third_party/upb/upb/reflection/message_reserved_range_internal.h",
                "third_party/upb/upb/reflection/method_def.h",
                "third_party/upb/upb/reflection/method_def_internal.h",
                "third_party/upb/upb/reflection/oneof_def.h",
                "third_party/upb/upb/reflection/oneof_def_internal.h",
                "third_party/upb/upb/reflection/service_def.h",
                "third_party/upb/upb/reflection/service_def_internal.h",
                "third_party/upb/upb/status.h",
                "third_party/upb/upb/string_view.h",
                "third_party/upb/upb/text/encode.h",
                "third_party/upb/upb/text_encode.h",
                "third_party/upb/upb/upb.h",
                "third_party/upb/upb/upb.hpp",
                "third_party/upb/upb/wire/common.h",
                "third_party/upb/upb/wire/common_internal.h",
                "third_party/upb/upb/wire/decode_fast.h",
                "third_party/upb/upb/wire/decode.h",
                "third_party/upb/upb/wire/decode_internal.h",
                "third_party/upb/upb/wire/encode.h",
                "third_party/upb/upb/wire/eps_copy_input_stream.h",
                "third_party/upb/upb/wire/reader.h",
                "third_party/upb/upb/wire/swap_internal.h",
                "third_party/upb/upb/wire/types.h",
                "src/core/ext/upb-generated/google/protobuf/descriptor.upb.h",
                "src/core/ext/upbdefs-generated/google/protobuf/descriptor.upbdefs.h",
            ],
            "secure": False,
        }
    ]
except:
    pass

print(yaml.dump(out))
