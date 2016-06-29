Pod::Spec.new do |s|
  s.name     = "RemoteTest"
  s.version  = "0.0.1"
  s.license  = "New BSD"
  s.authors  = { 'gRPC contributors' => 'grpc-io@googlegroups.com' }
  s.homepage = "http://www.grpc.io/"
  s.summary = "RemoteTest example"
  s.source = { :git => 'https://github.com/grpc/grpc.git' }

  s.ios.deployment_target = '7.1'
  s.osx.deployment_target = '10.9'

  # Run protoc with the Objective-C and gRPC plugins to generate protocol messages and gRPC clients.
  repo_root = '../../../..'
  pods_root = "#{repo_root}/src/objective-c/tests/Pods"
  bin_dir = "#{repo_root}/bins/$CONFIG"

  protoc = "#{pods_root}/ProtoCompiler/protoc" # "#{bin_dir}/protobuf/protoc"
  plugin = "#{bin_dir}/grpc_objective_c_plugin"
  s.prepare_command = <<-CMD
    #{protoc} --plugin=protoc-gen-grpc=#{plugin} --objc_out=. --grpc_out=. *.proto
  CMD

  s.dependency "ProtoCompiler", "~> 3.0.0-beta-3.1"

  s.subspec "Messages" do |ms|
    ms.source_files = "*.pbobjc.{h,m}"
    ms.header_mappings_dir = "."
    ms.requires_arc = false
    ms.dependency "Protobuf", "~> 3.0.0-beta-3.1"
    # This is needed by all pods that depend on Protobuf:
    ms.pod_target_xcconfig = {
      'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=1',
    }
  end

  s.subspec "Services" do |ss|
    ss.source_files = "*.pbrpc.{h,m}"
    ss.header_mappings_dir = "."
    ss.requires_arc = true
    ss.dependency "gRPC-ProtoRPC", "~> 0.14"
    ss.dependency "#{s.name}/Messages"
  end
end
