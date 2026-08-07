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
require 'set' if Gem::Version.new(RUBY_VERSION) < Gem::Version.new('3.2')
require 'tsort'

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
          @files_by_name = {}
          @extensions_by_type = {}
          warm_index!
        end

        def server_reflection_info(requests, _call)
          # Lazy enumerator: respond as each request arrives.
          # An eager .map would deadlock clients that read before half-closing.
          Enumerator.new { |y| requests.each { |req| y << dispatch(req) } }
        end

        private

        # Build all indexes at construction time; frozen afterwards for thread safety.
        def warm_index!
          @service_names
            .filter_map { |name| @pool.lookup(name) }
            .each       { |desc| index_file_tree(desc.file_descriptor) }

          @files_by_name.freeze
          @extensions_by_type.each_value(&:freeze)
          @extensions_by_type.freeze
        end

        def index_file_tree(file_descriptor)
          fd_proto = file_descriptor.to_proto
          return if @files_by_name.key?(fd_proto.name)

          @files_by_name[fd_proto.name] = fd_proto
          index_extensions(fd_proto)
          index_symbol_deps(fd_proto)
        end

        def index_symbol_deps(fd_proto)
          type_references_in(fd_proto).each do |sym|
            dep_desc = @pool.lookup(sym)
            index_file_tree(dep_desc.file_descriptor) if dep_desc
          end
        end

        def type_references_in(fd_proto)
          symbols = Set.new
          fd_proto.message_type.each { |msg| extract_type_refs(msg, symbols) }
          fd_proto.service.each do |svc|
            svc['method'].each do |m| # string key avoids Kernel#method collision
              symbols << m.input_type.delete_prefix('.')  unless m.input_type.empty?
              symbols << m.output_type.delete_prefix('.') unless m.output_type.empty?
            end
          end
          fd_proto.extension.each { |ext| symbols << ext.extendee.delete_prefix('.') unless ext.extendee.empty? }
          symbols
        end

        def extract_type_refs(msg, symbols)
          msg.field.each     { |f|   symbols << f.type_name.delete_prefix('.')  unless f.type_name.empty? }
          msg.extension.each { |ext| symbols << ext.extendee.delete_prefix('.') unless ext.extendee.empty? }
          msg.nested_type.each { |nested| extract_type_refs(nested, symbols) }
        end

        def index_extensions(fd_proto)
          fd_proto.extension.each    { |ext| register_extension(ext, fd_proto) }
          fd_proto.message_type.each { |msg| index_nested_extensions(msg, fd_proto) }
        end

        def index_nested_extensions(msg, fd_proto)
          msg.extension.each   { |ext|    register_extension(ext, fd_proto) }
          msg.nested_type.each { |nested| index_nested_extensions(nested, fd_proto) }
        end

        def register_extension(ext, fd_proto)
          (@extensions_by_type[ext.extendee] ||= {})[ext.number] = fd_proto
        end

        def dispatch(req)
          handler = :"handle_#{req.message_request}"
          return unknown_request(req) unless respond_to?(handler, true)

          send(handler, req)
        end

        def handle_file_by_filename(req)
          fd_proto = @files_by_name[req.file_by_filename]
          fd_proto ? file_response(req, fd_proto) : not_found(req, req.file_by_filename)
        end

        def handle_file_containing_symbol(req)
          symbol = req.file_containing_symbol
          desc   = @pool.lookup(symbol)
          return not_found(req, symbol) unless desc

          # pool.lookup may return a FileDescriptor directly (for filenames) or
          # a message/service descriptor — normalize to the owning file.
          file_desc = desc.is_a?(Google::Protobuf::FileDescriptor) ? desc : desc.file_descriptor
          fd_proto = @files_by_name[file_desc.to_proto.name]
          fd_proto ? file_response(req, fd_proto) : not_found(req, symbol)
        end

        def handle_file_containing_extension(req)
          ext_req   = req.file_containing_extension
          type_name = ".#{ext_req.containing_type.delete_prefix('.')}"
          fd_proto  = @extensions_by_type.dig(type_name, ext_req.extension_number)
          fd_proto ? file_response(req, fd_proto) : not_found(req, "#{type_name}[#{ext_req.extension_number}]")
        end

        def handle_all_extension_numbers_of_type(req)
          type_name = ".#{req.all_extension_numbers_of_type.delete_prefix('.')}"
          exts      = @extensions_by_type.fetch(type_name, {})
          return not_found(req, type_name) if exts.empty?

          respond(req, all_extension_numbers_response: ExtensionNumberResponse.new(
            base_type_name: type_name,
            extension_number: exts.keys.sort
          ))
        end

        def handle_list_services(req)
          respond(req, list_services_response: ListServiceResponse.new(
            service: @service_names.map { |name| ServiceResponse.new(name: name) }
          ))
        end

        def file_response(req, fd_proto)
          respond(req, file_descriptor_response: FileDescriptorResponse.new(
            file_descriptor_proto: transitive_deps_of(fd_proto).map do |p|
              Google::Protobuf::FileDescriptorProto.encode(p)
            end
          ))
        end

        # BFS + TSort for dependency-first ordering.
        def transitive_deps_of(root)
          subgraph = {}
          queue    = [root]
          until queue.empty?
            node = queue.shift
            next if subgraph.key?(node.name)
            subgraph[node.name] = node
            node.dependency.filter_map { |n| @files_by_name[n] }.each { |d| queue << d }
          end

          TSort.tsort(
            subgraph.method(:each_key),
            ->(name, &b) { subgraph[name].dependency.each { |d| b.call(d) if subgraph.key?(d) } }
          ).map { |name| subgraph[name] }
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
