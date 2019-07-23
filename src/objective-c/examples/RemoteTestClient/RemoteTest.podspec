Pod::Spec.new do |s|
  s.name     = 'RemoteTest'
  s.version  = '0.0.1'
  s.license  = 'Apache License, Version 2.0'
  s.authors  = { 'gRPC contributors' => 'grpc-io@googlegroups.com' }
  s.homepage = 'https://grpc.io/'
  s.summary = 'RemoteTest example'
  s.source = { :git => 'https://github.com/grpc/grpc.git' }

  s.ios.deployment_target = '7.1'
  s.osx.deployment_target = '10.9'

  # Run protoc with the Objective-C and gRPC plugins to generate protocol messages and gRPC clients.
  s.dependency "!ProtoCompiler-gRPCPlugin"

  repo_root = '../../../..'
  bin_dir = "bins/$CONFIG"

  protoc = "#{bin_dir}/protobuf/protoc"
  well_known_types_dir = "third_party/protobuf/src"
  plugin = "#{bin_dir}/grpc_objective_c_plugin"
  out_dir = "src/objective-c/examples/RemoteTestClient"

  s.prepare_command = <<-CMD
    pushd #{repo_root}
    #{protoc} \
        --plugin=protoc-gen-grpc=#{plugin} \
        --objc_out=#{out_dir} \
        --grpc_out=#{out_dir} \
        -I . \
        -I #{well_known_types_dir} \
        #{out_dir}/*.proto
    popd
  CMD

  s.subspec 'Messages' do |ms|
    ms.source_files = '**/*.pbobjc.{h,m}'
    ms.header_mappings_dir = '.'
    ms.requires_arc = false
    ms.dependency 'Protobuf'
  end

  s.subspec 'Services' do |ss|
    ss.source_files = '**/*.pbrpc.{h,m}'
    ss.header_mappings_dir = '.'
    ss.requires_arc = true
    ss.dependency 'gRPC-ProtoRPC'
    ss.dependency "#{s.name}/Messages"
  end

  s.pod_target_xcconfig = {
    # This is needed by all pods that depend on Protobuf:
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=1',
    # This is needed by all pods that depend on gRPC-RxLibrary:
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES',
  }
end
