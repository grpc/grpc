# Copyright 2026 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# frozen_string_literal: true

require 'grpc'
require 'grpc/reflection/v1alpha/reflection_services_pb'
require 'google/protobuf/descriptor_pb'

module Grpc
  module Reflection
    module V1alpha
      SERVICE_NAME = 'grpc.reflection.v1alpha.ServerReflection'

      # Implements the gRPC Server Reflection protocol (v1alpha).
      # See README.md for usage.
      class Server < ServerReflection::Service
        StatusCodes = GRPC::Core::StatusCodes

        # @param service_names [Array<String>] fully-qualified service names to advertise
        # @param pool [Google::Protobuf::DescriptorPool] defaults to the generated pool
        def initialize(service_names, pool: nil)
          super()
          @service_names = service_names.sort.freeze
          @pool = pool || Google::Protobuf::DescriptorPool.generated_pool

          @list_services_response = ListServiceResponse.new(
            service: @service_names.map { |name| ServiceResponse.new(name: name) }
          ).freeze
        end

        def server_reflection_info(requests, _call)
          Enumerator.new { |y| requests.each { |req| y << dispatch(req) } }
        end

        private

        def dispatch(req)
          case req.message_request
          when :file_by_filename
            handle_file_by_filename(req)
          when :file_containing_symbol
            handle_file_containing_symbol(req)
          when :file_containing_extension
            handle_file_containing_extension(req)
          when :all_extension_numbers_of_type
            handle_all_extension_numbers_of_type(req)
          when :list_services
            handle_list_services(req)
          else
            unknown_request(req)
          end
        rescue StandardError => e
          respond(req, error_response: ErrorResponse.new(
            error_code: StatusCodes::INTERNAL,
            error_message: e.message
          ))
        end

        def handle_file_by_filename(req)
          fd = @pool.find_file_by_name(req.file_by_filename)
          fd ? file_response(req, fd) : not_found(req, req.file_by_filename)
        end

        def handle_file_containing_symbol(req)
          symbol = req.file_containing_symbol
          desc   = @pool.lookup(symbol)
          return not_found(req, symbol) unless desc

          file_desc = desc.is_a?(Google::Protobuf::FileDescriptor) ? desc : desc.file_descriptor
          file_desc ? file_response(req, file_desc) : not_found(req, symbol)
        end

        def handle_file_containing_extension(req)
          ext_req   = req.file_containing_extension
          type_name = ext_req.containing_type.delete_prefix('.')
          msg_desc  = @pool.lookup(type_name)

          unless msg_desc.is_a?(Google::Protobuf::Descriptor)
            return not_found(req, "#{ext_req.containing_type}[#{ext_req.extension_number}]")
          end

          ext_desc = @pool.find_extension_by_number(msg_desc, ext_req.extension_number)
          file_desc = ext_desc&.file_descriptor

          file_desc ? file_response(req, file_desc) : not_found(req, "#{ext_req.containing_type}[#{ext_req.extension_number}]")
        end

        def handle_all_extension_numbers_of_type(req)
          type_name = req.all_extension_numbers_of_type.delete_prefix('.')
          msg_desc  = @pool.lookup(type_name)

          unless msg_desc.is_a?(Google::Protobuf::Descriptor)
            return not_found(req, req.all_extension_numbers_of_type)
          end

          exts = @pool.find_all_extensions(msg_desc) || []

          respond(req, all_extension_numbers_response: ExtensionNumberResponse.new(
            base_type_name: type_name,
            extension_number: exts.map(&:number).sort
          ))
        end

        def handle_list_services(req)
          respond(req, list_services_response: @list_services_response)
        end

        def file_response(req, file_descriptor)
          descriptors = {}
          collect_transitive_dependencies(file_descriptor, descriptors)

          respond(req, file_descriptor_response: FileDescriptorResponse.new(
            file_descriptor_proto: descriptors.values.map do |fd|
              Google::Protobuf::FileDescriptorProto.encode(fd.to_proto)
            end
          ))
        end

        def collect_transitive_dependencies(file_descriptor, seen_files)
          seen_files[file_descriptor.name] = file_descriptor
          file_descriptor.dependencies.each do |dep|
            collect_transitive_dependencies(dep, seen_files) unless seen_files.key?(dep.name)
          end
        end

        def not_found(req, subject)
          respond(req, error_response: ErrorResponse.new(
            error_code: StatusCodes::NOT_FOUND,
            error_message: "#{subject} not found"
          ))
        end

        def unknown_request(req)
          respond(req, error_response: ErrorResponse.new(
            error_code: StatusCodes::INVALID_ARGUMENT,
            error_message: 'unknown request type'
          ))
        end

        def respond(req, **fields) = ServerReflectionResponse.new(original_request: req, **fields)
      end

      # Convenience: creates a Server and registers it on the given RpcServer.
      # Matches Python's enable_server_reflection().
      def self.enable_server_reflection(service_names, server, pool: nil)
        server.handle(Server.new(service_names, pool: pool))
      end
    end
  end
end
