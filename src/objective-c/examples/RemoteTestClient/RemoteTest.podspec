Pod::Spec.new do |s|
  s.name     = 'RemoteTest'
  s.version  = '0.0.1'
  s.license  = 'Apache License, Version 2.0'
  s.authors  = { 'gRPC contributors' => 'grpc-io@googlegroups.com' }
  s.homepage = 'https://grpc.io/'
  s.summary = 'RemoteTest example'
  s.source = { :git => 'https://github.com/grpc/grpc.git' }

  s.ios.deployment_target = '9.0'
  s.osx.deployment_target = '10.10'
  s.tvos.deployment_target = '10.0'
  s.watchos.deployment_target = '4.0'

  # Run protoc with the Objective-C and gRPC plugins to generate protocol messages and gRPC clients.
  s.dependency "!ProtoCompiler-gRPCPlugin"

  repo_root = '../../../..'
  bazel_exec_root = "#{repo_root}/bazel-out/darwin-fastbuild/bin"

  protoc = "#{bazel_exec_root}/external/com_google_protobuf/protoc"
  well_known_types_dir = "#{repo_root}/third_party/protobuf/src"
  plugin = "#{bazel_exec_root}/src/compiler/grpc_objective_c_plugin"

  # Since we switched to importing full path, -I needs to be set to the directory
  # from which the imported file can be found, which is the grpc's root here
  if ENV['FRAMEWORKS'] != 'NO' then
    s.user_target_xcconfig = { 'GCC_PREPROCESSOR_DEFINITIONS' => 'USE_FRAMEWORKS=1' }
    s.prepare_command = <<-CMD
    # Cannot find file if using *.proto. Maybe files' paths must match the -I flags
      #{protoc} \
          --plugin=protoc-gen-grpc=#{plugin} \
          --objc_out=. \
          --grpc_out=generate_for_named_framework=#{s.name}:. \
          --objc_opt=generate_for_named_framework=#{s.name} \
          -I #{repo_root} \
          -I #{well_known_types_dir} \
          #{repo_root}/src/objective-c/examples/RemoteTestClient/*.proto
    CMD
  else
    s.prepare_command = <<-CMD
      #{protoc} \
          --plugin=protoc-gen-grpc=#{plugin} \
          --objc_out=. \
          --grpc_out=. \
          -I #{repo_root} \
          -I #{well_known_types_dir} \
          #{repo_root}/src/objective-c/examples/RemoteTestClient/*.proto
    CMD
  end

  s.subspec 'Messages' do |ms|
    ms.source_files = 'src/objective-c/examples/RemoteTestClient/*.pbobjc.{h,m}'
    ms.header_mappings_dir = '.'
    ms.requires_arc = false
    ms.dependency 'Protobuf'
  end

  s.subspec 'Services' do |ss|
    ss.source_files = 'src/objective-c/examples/RemoteTestClient/*.pbrpc.{h,m}'
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
