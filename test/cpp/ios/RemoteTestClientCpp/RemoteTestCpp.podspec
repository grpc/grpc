Pod::Spec.new do |s|
  s.name     = "RemoteTestCpp"
  s.version  = "0.0.1"
  s.license  = "Apache License, Version 2.0"
  s.authors  = { 'gRPC contributors' => 'grpc-io@googlegroups.com' }
  s.homepage = "https://grpc.io/"
  s.summary = "RemoteTest example"
  s.source = { :git => 'https://github.com/grpc/grpc.git' }

  s.ios.deployment_target = '9.0'
  s.osx.deployment_target = '10.10'

  # Run protoc with the C++ and gRPC plugins to generate protocol messages and gRPC clients.
  s.dependency "!ProtoCompiler-gRPCCppPlugin"
  s.dependency "Protobuf-C++"
  s.dependency "gRPC-C++"
  s.source_files = "src/proto/grpc/testing/*.pb.{h,cc}"
  s.header_mappings_dir = "."
  s.requires_arc = false

  repo_root = '../../../..'
  bazel_exec_root = "#{repo_root}/bazel-out/darwin-fastbuild/bin"

  protoc = "#{bazel_exec_root}/external/com_google_protobuf/protoc"
  well_known_types_dir = "#{repo_root}/third_party/protobuf/src"
  plugin = "#{bazel_exec_root}/src/compiler/grpc_cpp_plugin"
  proto_dir = "#{repo_root}/src/proto/grpc/testing"

  s.prepare_command = <<-CMD
    #{protoc} \
        --plugin=protoc-gen-grpc=#{plugin} \
        --cpp_out=. \
        --grpc_out=. \
        -I #{repo_root} \
        -I #{well_known_types_dir} \
        #{proto_dir}/echo.proto #{proto_dir}/echo_messages.proto #{proto_dir}/simple_messages.proto
  CMD

  s.pod_target_xcconfig = {
    # This is needed by all pods that depend on Protobuf:
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=1 GPB_GRPC_FORWARD_DECLARE_MESSAGE_PROTO=1',
    # This is needed by all pods that depend on gRPC-RxLibrary:
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES',
  }
end
