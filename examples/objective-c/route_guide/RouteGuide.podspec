Pod::Spec.new do |s|
  s.name     = "RouteGuide"
  s.version  = "0.0.1"
  s.license  = "New BSD"

  s.ios.deployment_target = "7.1"
  s.osx.deployment_target = "10.9"

  # Base directory where the .proto files are.
  src = "../../protos"

  # Directory where the generated files will be placed.
  dir = "Pods/" + s.name

  # Run protoc with the Objective-C and gRPC plugins to generate protocol messages and gRPC clients.
  s.prepare_command = <<-CMD
    mkdir -p #{dir}
    protoc -I #{src} --objc_out=#{dir} --objcgrpc_out=#{dir} #{src}/route_guide.proto
  CMD

  s.subspec "Messages" do |ms|
    ms.source_files = "#{dir}/*.pbobjc.{h,m}", "#{dir}/**/*.pbobjc.{h,m}"
    ms.header_mappings_dir = dir
    ms.requires_arc = false
    ms.dependency "Protobuf", "~> 3.0.0-alpha-4"
  end

  s.subspec "Services" do |ss|
    ss.source_files = "#{dir}/*.pbrpc.{h,m}", "#{dir}/**/*.pbrpc.{h,m}"
    ss.header_mappings_dir = dir
    ss.requires_arc = true
    ss.dependency "gRPC", "~> 0.11"
    ss.dependency "#{s.name}/Messages"
  end
end
