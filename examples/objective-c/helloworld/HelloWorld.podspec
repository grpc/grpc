Pod::Spec.new do |s|
  s.name     = "HelloWorld"
  s.version  = "0.0.1"
  s.license  = "New BSD"
  s.authors  = { 'gRPC contributors' => 'grpc-io@googlegroups.com' }
  s.homepage = "http://www.grpc.io/"
  s.summary = "HelloWorld example"
  s.source = { :git => 'https://github.com/grpc/grpc.git' }

  s.ios.deployment_target = "7.1"
  s.osx.deployment_target = "10.9"

  # Base directory where the .proto files are.
  src = "../../protos"

  # Directory where the generated files will be placed.
  dir = "Pods/" + s.name

  # Run protoc with the Objective-C and gRPC plugins to generate protocol messages and gRPC clients.
  s.dependency "!ProtoCompiler-gRPCPlugin", "~> 0.14"

  repo_root = '../../..'
  pods_root = "#{repo_root}/examples/objective-c/helloworld/Pods"

  protoc_dir = "#{pods_root}/!ProtoCompiler"
  protoc = "#{protoc_dir}/protoc"

  plugin = "#{pods_root}/!ProtoCompiler-gRPCPlugin/grpc_objective_c_plugin"

  s.prepare_command = <<-CMD
    mkdir -p #{dir}
    #{protoc} \
        --plugin=protoc-gen-grpc=#{plugin} \
        --objc_out=#{dir} \
        --grpc_out=#{dir} \
        -I #{src} \
        -I #{protoc_dir} \
        #{src}/helloworld.proto
  CMD

  s.subspec "Messages" do |ms|
    ms.source_files = "#{dir}/*.pbobjc.{h,m}", "#{dir}/**/*.pbobjc.{h,m}"
    ms.header_mappings_dir = dir
    ms.requires_arc = false
    ms.dependency "Protobuf"
    # This is needed by all pods that depend on Protobuf:
    ms.pod_target_xcconfig = {
      'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=1',
    }
  end

  s.subspec "Services" do |ss|
    ss.source_files = "#{dir}/*.pbrpc.{h,m}", "#{dir}/**/*.pbrpc.{h,m}"
    ss.header_mappings_dir = dir
    ss.requires_arc = true
    ss.dependency "gRPC-ProtoRPC"
    ss.dependency "#{s.name}/Messages"
  end
end
