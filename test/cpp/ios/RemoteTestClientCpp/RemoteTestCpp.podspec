Pod::Spec.new do |s|
  s.name     = "RemoteTestCpp"
  s.version  = "0.0.1"
  s.license  = "Apache License, Version 2.0"
  s.authors  = { 'gRPC contributors' => 'grpc-io@googlegroups.com' }
  s.homepage = "https://grpc.io/"
  s.summary = "RemoteTest example"
  s.source = { :git => 'https://github.com/grpc/grpc.git' }

  s.ios.deployment_target = '7.1'
  s.osx.deployment_target = '10.9'

  # Run protoc with the C++ and gRPC plugins to generate protocol messages and gRPC clients.
  s.dependency "!ProtoCompiler-gRPCCppPlugin"
  s.dependency "Protobuf-C++"
  s.dependency "gRPC-C++"
  s.source_files = "test/proto/*.pb.{h,cc}"
  s.header_mappings_dir = "RemoteTestCpp"
  s.requires_arc = false

  repo_root = '../../../..'
  config = ENV['CONFIG'] || 'opt'
  bin_dir = "#{repo_root}/bins/#{config}"

  protoc = "#{bin_dir}/protobuf/protoc"
  well_known_types_dir = "#{repo_root}/third_party/protobuf/src"
  plugin = "#{bin_dir}/grpc_cpp_plugin"
  proto_dir = "#{repo_root}/test/proto"

  s.prepare_command = <<-CMD
    if [ -f #{protoc} ]; then
      #{protoc} \
          --plugin=protoc-gen-grpc=#{plugin} \
          --cpp_out=. \
          --grpc_out=. \
          -I #{repo_root} \
          -I #{proto_dir} \
          -I #{well_known_types_dir} \
          #{proto_dir}/echo.proto
      #{protoc} \
          --plugin=protoc-gen-grpc=#{plugin} \
          --cpp_out=. \
          --grpc_out=. \
          -I #{repo_root} \
          -I #{proto_dir} \
          -I #{well_known_types_dir} \
          #{proto_dir}/echo_messages.proto
      #{protoc} \
          --plugin=protoc-gen-grpc=#{plugin} \
          --cpp_out=. \
          --grpc_out=. \
          -I #{repo_root} \
          -I #{proto_dir} \
          -I #{well_known_types_dir} \
          #{proto_dir}/simple_messages.proto
    else
      # protoc was not found bin_dir, use installed version instead

      if ! [ -x "$(command -v protoc)" ]; then
        (>&2 echo "\nERROR: protoc not found")
        exit 1
      fi
      (>&2 echo "\nWARNING: Using installed version of protoc. It might be incompatible with gRPC")

      protoc \
          --plugin=protoc-gen-grpc=#{plugin} \
          --cpp_out=. \
          --grpc_out=. \
          -I #{repo_root} \
          -I #{proto_dir} \
          -I #{well_known_types_dir} \
          #{proto_dir}/echo.proto
      protoc \
          --plugin=protoc-gen-grpc=#{plugin} \
          --cpp_out=. \
          --grpc_out=. \
          -I #{repo_root} \
          -I #{proto_dir} \
          -I #{well_known_types_dir} \
          #{proto_dir}/echo_messages.proto
      protoc \
          --plugin=protoc-gen-grpc=#{plugin} \
          --cpp_out=. \
          --grpc_out=. \
          -I #{repo_root} \
          -I #{proto_dir} \
          -I #{well_known_types_dir} \
          #{proto_dir}/simple_messages.proto
    fi
  CMD

  s.pod_target_xcconfig = {
    # This is needed by all pods that depend on Protobuf:
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=1 GPB_GRPC_FORWARD_DECLARE_MESSAGE_PROTO=1',
    # This is needed by all pods that depend on gRPC-RxLibrary:
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES',
  }
end
