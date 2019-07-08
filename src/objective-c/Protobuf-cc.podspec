Pod::Spec.new do |s|
  s.name     = 'Protobuf-cc'
  s.version = '3.8.0'
  s.summary  = 'Protocol Buffers v3 runtime library for C++.'
  s.homepage = 'https://github.com/google/protobuf'
  s.license  = '3-Clause BSD License'
  s.authors  = { 'The Protocol Buffers contributors' => 'protobuf@googlegroups.com' }
  s.cocoapods_version = '>= 1.0'

  s.source = { :git => 'https://github.com/google/protobuf.git',
               :tag => "v#{s.version}" }

  s.source_files = 'src/google/protobuf/*.{h,cc,inc}',
                   'src/google/protobuf/stubs/*.{h,cc}',
                   'src/google/protobuf/io/*.{h,cc}',
                   'src/google/protobuf/util/*.{h,cc}',
                   'src/google/protobuf/util/internal/*.{h,cc}'

  # Excluding all the tests in the directories above
  s.exclude_files = 'src/google/protobuf/any_test.cc',
                    'src/google/protobuf/arenastring_unittest.cc',
                    'src/google/protobuf/arena_test_util.cc',
                    'src/google/protobuf/arena_test_util.h',
                    'src/google/protobuf/arena_unittest.cc',
                    'src/google/protobuf/descriptor_database_unittest.cc',
                    'src/google/protobuf/descriptor_unittest.cc',
                    'src/google/protobuf/drop_unknown_fields_test.cc',
                    'src/google/protobuf/dynamic_message_unittest.cc',
                    'src/google/protobuf/extension_set_unittest.cc',
                    'src/google/protobuf/generated_message_reflection_unittest.cc',
                    'src/google/protobuf/lite_arena_unittest.cc',
                    'src/google/protobuf/lite_unittest.cc',
                    'src/google/protobuf/map_field_test.cc',
                    'src/google/protobuf/map_lite_test_util.cc',
                    'src/google/protobuf/map_lite_test_util.h',
                    'src/google/protobuf/map_test.cc',
                    'src/google/protobuf/map_test_util.cc',
                    'src/google/protobuf/map_test_util.h',
                    'src/google/protobuf/map_test_util_impl.h',
                    'src/google/protobuf/message_unittest.cc',
                    'src/google/protobuf/no_field_presence_test.cc',
                    'src/google/protobuf/preserve_unknown_enum_test.cc',
                    'src/google/protobuf/proto3_arena_lite_unittest.cc',
                    'src/google/protobuf/proto3_arena_unittest.cc',
                    'src/google/protobuf/proto3_lite_unittest.cc',
                    'src/google/protobuf/reflection_ops_unittest.cc',
                    'src/google/protobuf/repeated_field_reflection_unittest.cc',
                    'src/google/protobuf/repeated_field_unittest.cc',
                    'src/google/protobuf/test_util.cc',
                    'src/google/protobuf/test_util.h',
                    'src/google/protobuf/test_util_lite.cc',
                    'src/google/protobuf/test_util_lite.h',
                    'src/google/protobuf/text_format_unittest.cc',
                    'src/google/protobuf/unknown_field_set_unittest.cc',
                    'src/google/protobuf/well_known_types_unittest.cc',
                    'src/google/protobuf/wire_format_unittest.cc',
                    'src/google/protobuf/stubs/bytestream_unittest.cc',
                    'src/google/protobuf/stubs/common_unittest.cc',
                    'src/google/protobuf/stubs/int128_unittest.cc',
                    'src/google/protobuf/stubs/io_win32_unittest.cc',
                    'src/google/protobuf/stubs/once_unittest.cc',
                    'src/google/protobuf/stubs/statusor_test.cc',
                    'src/google/protobuf/stubs/status_test.cc',
                    'src/google/protobuf/stubs/stringpiece_unittest.cc',
                    'src/google/protobuf/stubs/stringprintf_unittest.cc',
                    'src/google/protobuf/stubs/structurally_valid_unittest.cc',
                    'src/google/protobuf/stubs/strutil_unittest.cc',
                    'src/google/protobuf/stubs/template_util_unittest.cc',
                    'src/google/protobuf/stubs/time_test.cc',
                    'src/google/protobuf/stubs/type_traits_unittest.cc',
                    'src/google/protobuf/io/coded_stream_unittest.cc',
                    'src/google/protobuf/io/printer_unittest.cc',
                    'src/google/protobuf/io/tokenizer_unittest.cc',
                    'src/google/protobuf/io/zero_copy_stream_unittest.cc',
                    'src/google/protobuf/util/type_resolver_util_test.cc',
                    'src/google/protobuf/util/time_util_test.cc',
                    'src/google/protobuf/util/json_util_test.cc',
                    'src/google/protobuf/util/field_mask_util_test.cc',
                    'src/google/protobuf/util/field_comparator_test.cc',
                    'src/google/protobuf/util/delimited_message_util_test.cc',
                    'src/google/protobuf/util/message_differencer_unittest.cc',
                    'src/google/protobuf/util/internal/json_stream_parser_test.cc',
                    'src/google/protobuf/util/internal/default_value_objectwriter_test.cc',
                    'src/google/protobuf/util/internal/json_objectwriter_test.cc',
                    'src/google/protobuf/util/internal/protostream_objectsource_test.cc',
                    'src/google/protobuf/util/internal/protostream_objectwriter_test.cc',
                    'src/google/protobuf/util/internal/protostream_objectwriter_test.cc'

  s.header_mappings_dir = 'src'
  s.libraries = 'c++'

  s.ios.deployment_target = '7.0'
  s.osx.deployment_target = '10.9'
  s.tvos.deployment_target = '9.0'
  s.watchos.deployment_target = '2.0'
  s.requires_arc = false

  s.pod_target_xcconfig = {
    # Do not let src/google/protobuf/stub/time.h override system API
    'USE_HEADERMAP' => 'NO',
    'ALWAYS_SEARCH_USER_PATHS' => 'NO',

    # Configure tool is not being used for Xcode. When building, assume pthread is supported.
    'GCC_PREPROCESSOR_DEFINITIONS' => '"$(inherited)" "COCOAPODS=1" "HAVE_PTHREAD=1"',
  }

end
