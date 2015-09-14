Pod::Spec.new do |s|
  s.name     = "RemoteTest"
  s.version  = "0.0.1"
  s.license  = "New BSD"

  s.ios.deployment_target = "6.0"
  s.osx.deployment_target = "10.8"

  # Run protoc with the Objective-C and gRPC plugins to generate protocol messages and gRPC clients.
  s.prepare_command = <<-CMD
    protoc --objc_out=. --objcgrpc_out=. *.proto
  CMD

  s.subspec "Messages" do |ms|
    ms.source_files = "*.pbobjc.{h,m}"
    ms.header_mappings_dir = "."
    ms.requires_arc = false
    ms.dependency "Protobuf", "~> 3.0.0-alpha-4"
  end

  s.subspec "Services" do |ss|
    ss.source_files = "*.pbrpc.{h,m}"
    ss.header_mappings_dir = "."
    ss.requires_arc = true
    ss.dependency "gRPC", "~> 0.7"
    ss.dependency "#{s.name}/Messages"
  end
end
