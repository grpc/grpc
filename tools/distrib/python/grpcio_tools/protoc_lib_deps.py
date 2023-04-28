
# Copyright 2017 gRPC authors.
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

# AUTO-GENERATED BY make_grpcio_tools.py!
CC_FILES=[
    'third_party/abseil-cpp/absl/base/internal/cycleclock.cc',
    'third_party/abseil-cpp/absl/base/internal/low_level_alloc.cc',
    'third_party/abseil-cpp/absl/base/internal/raw_logging.cc',
    'third_party/abseil-cpp/absl/base/internal/spinlock.cc',
    'third_party/abseil-cpp/absl/base/internal/spinlock_wait.cc',
    'third_party/abseil-cpp/absl/base/internal/strerror.cc',
    'third_party/abseil-cpp/absl/base/internal/sysinfo.cc',
    'third_party/abseil-cpp/absl/base/internal/thread_identity.cc',
    'third_party/abseil-cpp/absl/base/internal/throw_delegate.cc',
    'third_party/abseil-cpp/absl/base/internal/unscaledcycleclock.cc',
    'third_party/abseil-cpp/absl/base/log_severity.cc',
    'third_party/abseil-cpp/absl/container/internal/hashtablez_sampler.cc',
    'third_party/abseil-cpp/absl/container/internal/hashtablez_sampler_force_weak_definition.cc',
    'third_party/abseil-cpp/absl/container/internal/raw_hash_set.cc',
    'third_party/abseil-cpp/absl/crc/crc32c.cc',
    'third_party/abseil-cpp/absl/crc/internal/cpu_detect.cc',
    'third_party/abseil-cpp/absl/crc/internal/crc.cc',
    'third_party/abseil-cpp/absl/crc/internal/crc_cord_state.cc',
    'third_party/abseil-cpp/absl/crc/internal/crc_memcpy_fallback.cc',
    'third_party/abseil-cpp/absl/crc/internal/crc_memcpy_x86_64.cc',
    'third_party/abseil-cpp/absl/crc/internal/crc_non_temporal_memcpy.cc',
    'third_party/abseil-cpp/absl/crc/internal/crc_x86_arm_combined.cc',
    'third_party/abseil-cpp/absl/debugging/internal/address_is_readable.cc',
    'third_party/abseil-cpp/absl/debugging/internal/demangle.cc',
    'third_party/abseil-cpp/absl/debugging/internal/elf_mem_image.cc',
    'third_party/abseil-cpp/absl/debugging/internal/examine_stack.cc',
    'third_party/abseil-cpp/absl/debugging/internal/vdso_support.cc',
    'third_party/abseil-cpp/absl/debugging/stacktrace.cc',
    'third_party/abseil-cpp/absl/debugging/symbolize.cc',
    'third_party/abseil-cpp/absl/hash/internal/city.cc',
    'third_party/abseil-cpp/absl/hash/internal/hash.cc',
    'third_party/abseil-cpp/absl/hash/internal/low_level_hash.cc',
    'third_party/abseil-cpp/absl/log/globals.cc',
    'third_party/abseil-cpp/absl/log/initialize.cc',
    'third_party/abseil-cpp/absl/log/internal/check_op.cc',
    'third_party/abseil-cpp/absl/log/internal/conditions.cc',
    'third_party/abseil-cpp/absl/log/internal/globals.cc',
    'third_party/abseil-cpp/absl/log/internal/log_format.cc',
    'third_party/abseil-cpp/absl/log/internal/log_message.cc',
    'third_party/abseil-cpp/absl/log/internal/log_sink_set.cc',
    'third_party/abseil-cpp/absl/log/internal/nullguard.cc',
    'third_party/abseil-cpp/absl/log/internal/proto.cc',
    'third_party/abseil-cpp/absl/log/log_entry.cc',
    'third_party/abseil-cpp/absl/log/log_sink.cc',
    'third_party/abseil-cpp/absl/numeric/int128.cc',
    'third_party/abseil-cpp/absl/profiling/internal/exponential_biased.cc',
    'third_party/abseil-cpp/absl/status/status.cc',
    'third_party/abseil-cpp/absl/status/status_payload_printer.cc',
    'third_party/abseil-cpp/absl/status/statusor.cc',
    'third_party/abseil-cpp/absl/strings/ascii.cc',
    'third_party/abseil-cpp/absl/strings/charconv.cc',
    'third_party/abseil-cpp/absl/strings/cord.cc',
    'third_party/abseil-cpp/absl/strings/cord_analysis.cc',
    'third_party/abseil-cpp/absl/strings/cord_buffer.cc',
    'third_party/abseil-cpp/absl/strings/escaping.cc',
    'third_party/abseil-cpp/absl/strings/internal/charconv_bigint.cc',
    'third_party/abseil-cpp/absl/strings/internal/charconv_parse.cc',
    'third_party/abseil-cpp/absl/strings/internal/cord_internal.cc',
    'third_party/abseil-cpp/absl/strings/internal/cord_rep_btree.cc',
    'third_party/abseil-cpp/absl/strings/internal/cord_rep_btree_navigator.cc',
    'third_party/abseil-cpp/absl/strings/internal/cord_rep_btree_reader.cc',
    'third_party/abseil-cpp/absl/strings/internal/cord_rep_consume.cc',
    'third_party/abseil-cpp/absl/strings/internal/cord_rep_crc.cc',
    'third_party/abseil-cpp/absl/strings/internal/cord_rep_ring.cc',
    'third_party/abseil-cpp/absl/strings/internal/cordz_functions.cc',
    'third_party/abseil-cpp/absl/strings/internal/cordz_handle.cc',
    'third_party/abseil-cpp/absl/strings/internal/cordz_info.cc',
    'third_party/abseil-cpp/absl/strings/internal/damerau_levenshtein_distance.cc',
    'third_party/abseil-cpp/absl/strings/internal/escaping.cc',
    'third_party/abseil-cpp/absl/strings/internal/memutil.cc',
    'third_party/abseil-cpp/absl/strings/internal/ostringstream.cc',
    'third_party/abseil-cpp/absl/strings/internal/str_format/arg.cc',
    'third_party/abseil-cpp/absl/strings/internal/str_format/bind.cc',
    'third_party/abseil-cpp/absl/strings/internal/str_format/extension.cc',
    'third_party/abseil-cpp/absl/strings/internal/str_format/float_conversion.cc',
    'third_party/abseil-cpp/absl/strings/internal/str_format/output.cc',
    'third_party/abseil-cpp/absl/strings/internal/str_format/parser.cc',
    'third_party/abseil-cpp/absl/strings/internal/stringify_sink.cc',
    'third_party/abseil-cpp/absl/strings/internal/utf8.cc',
    'third_party/abseil-cpp/absl/strings/match.cc',
    'third_party/abseil-cpp/absl/strings/numbers.cc',
    'third_party/abseil-cpp/absl/strings/str_cat.cc',
    'third_party/abseil-cpp/absl/strings/str_replace.cc',
    'third_party/abseil-cpp/absl/strings/str_split.cc',
    'third_party/abseil-cpp/absl/strings/string_view.cc',
    'third_party/abseil-cpp/absl/strings/substitute.cc',
    'third_party/abseil-cpp/absl/synchronization/barrier.cc',
    'third_party/abseil-cpp/absl/synchronization/blocking_counter.cc',
    'third_party/abseil-cpp/absl/synchronization/internal/create_thread_identity.cc',
    'third_party/abseil-cpp/absl/synchronization/internal/graphcycles.cc',
    'third_party/abseil-cpp/absl/synchronization/internal/per_thread_sem.cc',
    'third_party/abseil-cpp/absl/synchronization/internal/waiter.cc',
    'third_party/abseil-cpp/absl/synchronization/mutex.cc',
    'third_party/abseil-cpp/absl/synchronization/notification.cc',
    'third_party/abseil-cpp/absl/time/civil_time.cc',
    'third_party/abseil-cpp/absl/time/clock.cc',
    'third_party/abseil-cpp/absl/time/duration.cc',
    'third_party/abseil-cpp/absl/time/format.cc',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/civil_time_detail.cc',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_fixed.cc',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_format.cc',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_if.cc',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_impl.cc',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_info.cc',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_libc.cc',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_lookup.cc',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_posix.cc',
    'third_party/abseil-cpp/absl/time/internal/cctz/src/zone_info_source.cc',
    'third_party/abseil-cpp/absl/time/time.cc',
    'third_party/abseil-cpp/absl/types/bad_optional_access.cc',
    'third_party/abseil-cpp/absl/types/bad_variant_access.cc',
    'third_party/protobuf/src/google/protobuf/any.cc',
    'third_party/protobuf/src/google/protobuf/any_lite.cc',
    'third_party/protobuf/src/google/protobuf/arena.cc',
    'third_party/protobuf/src/google/protobuf/arena_align.cc',
    'third_party/protobuf/src/google/protobuf/arena_config.cc',
    'third_party/protobuf/src/google/protobuf/arenastring.cc',
    'third_party/protobuf/src/google/protobuf/arenaz_sampler.cc',
    'third_party/protobuf/src/google/protobuf/compiler/allowlists/empty_package.cc',
    'third_party/protobuf/src/google/protobuf/compiler/allowlists/weak_imports.cc',
    'third_party/protobuf/src/google/protobuf/compiler/code_generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/command_line_interface.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/enum.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/extension.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/field_generators/cord_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/field_generators/enum_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/field_generators/map_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/field_generators/message_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/field_generators/primitive_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/field_generators/string_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/file.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/helpers.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/message.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/padding_optimizer.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/parse_function_generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/service.cc',
    'third_party/protobuf/src/google/protobuf/compiler/cpp/tracker.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_doc_comment.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_enum.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_enum_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_field_base.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_helpers.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_map_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_message.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_message_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_primitive_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_reflection_class.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_repeated_enum_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_repeated_message_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_repeated_primitive_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_source_generator_base.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/csharp_wrapper_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/csharp/names.cc',
    'third_party/protobuf/src/google/protobuf/compiler/importer.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/context.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/doc_comment.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/enum.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/enum_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/enum_field_lite.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/enum_lite.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/extension.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/extension_lite.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/file.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/generator_factory.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/helpers.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/kotlin_generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/map_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/map_field_lite.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/message.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/message_builder.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/message_builder_lite.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/message_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/message_field_lite.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/message_lite.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/message_serialization.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/name_resolver.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/names.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/primitive_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/primitive_field_lite.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/service.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/shared_code_generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/string_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/java/string_field_lite.cc',
    'third_party/protobuf/src/google/protobuf/compiler/main.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/enum.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/enum_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/extension.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/file.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/helpers.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/import_writer.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/line_consumer.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/map_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/message.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/message_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/names.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/oneof.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/primitive_field.cc',
    'third_party/protobuf/src/google/protobuf/compiler/objectivec/text_format_decode_data.cc',
    'third_party/protobuf/src/google/protobuf/compiler/parser.cc',
    'third_party/protobuf/src/google/protobuf/compiler/php/names.cc',
    'third_party/protobuf/src/google/protobuf/compiler/php/php_generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/plugin.cc',
    'third_party/protobuf/src/google/protobuf/compiler/plugin.pb.cc',
    'third_party/protobuf/src/google/protobuf/compiler/python/generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/python/helpers.cc',
    'third_party/protobuf/src/google/protobuf/compiler/python/pyi_generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/retention.cc',
    'third_party/protobuf/src/google/protobuf/compiler/ruby/ruby_generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/rust/accessors/accessors.cc',
    'third_party/protobuf/src/google/protobuf/compiler/rust/accessors/singular_bytes.cc',
    'third_party/protobuf/src/google/protobuf/compiler/rust/accessors/singular_scalar.cc',
    'third_party/protobuf/src/google/protobuf/compiler/rust/context.cc',
    'third_party/protobuf/src/google/protobuf/compiler/rust/generator.cc',
    'third_party/protobuf/src/google/protobuf/compiler/rust/message.cc',
    'third_party/protobuf/src/google/protobuf/compiler/rust/naming.cc',
    'third_party/protobuf/src/google/protobuf/compiler/subprocess.cc',
    'third_party/protobuf/src/google/protobuf/compiler/zip_writer.cc',
    'third_party/protobuf/src/google/protobuf/descriptor.cc',
    'third_party/protobuf/src/google/protobuf/descriptor.pb.cc',
    'third_party/protobuf/src/google/protobuf/descriptor_database.cc',
    'third_party/protobuf/src/google/protobuf/dynamic_message.cc',
    'third_party/protobuf/src/google/protobuf/extension_set.cc',
    'third_party/protobuf/src/google/protobuf/extension_set_heavy.cc',
    'third_party/protobuf/src/google/protobuf/generated_enum_util.cc',
    'third_party/protobuf/src/google/protobuf/generated_message_bases.cc',
    'third_party/protobuf/src/google/protobuf/generated_message_reflection.cc',
    'third_party/protobuf/src/google/protobuf/generated_message_tctable_full.cc',
    'third_party/protobuf/src/google/protobuf/generated_message_tctable_gen.cc',
    'third_party/protobuf/src/google/protobuf/generated_message_tctable_lite.cc',
    'third_party/protobuf/src/google/protobuf/generated_message_util.cc',
    'third_party/protobuf/src/google/protobuf/implicit_weak_message.cc',
    'third_party/protobuf/src/google/protobuf/inlined_string_field.cc',
    'third_party/protobuf/src/google/protobuf/io/coded_stream.cc',
    'third_party/protobuf/src/google/protobuf/io/gzip_stream.cc',
    'third_party/protobuf/src/google/protobuf/io/io_win32.cc',
    'third_party/protobuf/src/google/protobuf/io/printer.cc',
    'third_party/protobuf/src/google/protobuf/io/strtod.cc',
    'third_party/protobuf/src/google/protobuf/io/tokenizer.cc',
    'third_party/protobuf/src/google/protobuf/io/zero_copy_sink.cc',
    'third_party/protobuf/src/google/protobuf/io/zero_copy_stream.cc',
    'third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl.cc',
    'third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl_lite.cc',
    'third_party/protobuf/src/google/protobuf/map.cc',
    'third_party/protobuf/src/google/protobuf/map_field.cc',
    'third_party/protobuf/src/google/protobuf/message.cc',
    'third_party/protobuf/src/google/protobuf/message_lite.cc',
    'third_party/protobuf/src/google/protobuf/parse_context.cc',
    'third_party/protobuf/src/google/protobuf/port.cc',
    'third_party/protobuf/src/google/protobuf/reflection_mode.cc',
    'third_party/protobuf/src/google/protobuf/reflection_ops.cc',
    'third_party/protobuf/src/google/protobuf/repeated_field.cc',
    'third_party/protobuf/src/google/protobuf/repeated_ptr_field.cc',
    'third_party/protobuf/src/google/protobuf/service.cc',
    'third_party/protobuf/src/google/protobuf/stubs/common.cc',
    'third_party/protobuf/src/google/protobuf/text_format.cc',
    'third_party/protobuf/src/google/protobuf/unknown_field_set.cc',
    'third_party/protobuf/src/google/protobuf/wire_format.cc',
    'third_party/protobuf/src/google/protobuf/wire_format_lite.cc',
    'third_party/utf8_range/utf8_validity.cc'
]

PROTO_FILES=[
    'google/protobuf/any.proto',
    'google/protobuf/api.proto',
    'google/protobuf/compiler/plugin.proto',
    'google/protobuf/descriptor.proto',
    'google/protobuf/duration.proto',
    'google/protobuf/empty.proto',
    'google/protobuf/field_mask.proto',
    'google/protobuf/source_context.proto',
    'google/protobuf/struct.proto',
    'google/protobuf/timestamp.proto',
    'google/protobuf/type.proto',
    'google/protobuf/wrappers.proto'
]

CC_INCLUDES=[
 'third_party/abseil-cpp', 'third_party/protobuf/src', 'third_party/utf8_range'
]
PROTO_INCLUDE='third_party/protobuf/src'

PROTOBUF_SUBMODULE_VERSION="51f51ac4efc3a1146e10416d31d24e0052cd9d86"
