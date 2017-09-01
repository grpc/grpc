Files generated for use by Census stats and trace recording subsystem.

# Files
* census.pb.{h,c} - Generated from src/core/ext/census/census.proto, using the
  script `tools/codegen/core/gen_nano_proto.sh src/proto/census/census.proto
  $PWD/src/core/ext/census/gen src/core/ext/census/gen`
* trace_context.pb.{h,c} - Generated from
  src/core/ext/census/trace_context.proto, using the script
  `tools/codegen/core/gen_nano_proto.sh src/proto/census/trace_context.proto
  $PWD/src/core/ext/census/gen src/core/ext/census/gen`
