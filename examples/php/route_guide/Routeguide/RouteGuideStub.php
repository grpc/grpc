<?php
// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
namespace Routeguide;

/**
 * Interface exported by the server.
 */
class RouteGuideStub {

    /**
     * A simple RPC.
     *
     * Obtains the feature at a given position.
     *
     * A feature with an empty name is returned if there's no feature at the given
     * position.
     * @param \Routeguide\Point $request client request
     * @param \Grpc\ServerContext $context server request context
     * @return \Routeguide\Feature for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function GetFeature(
        \Routeguide\Point $request,
        \Grpc\ServerContext $context
    ): ?\Routeguide\Feature {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * A server-to-client streaming RPC.
     *
     * Obtains the Features available within the given Rectangle.  Results are
     * streamed rather than returned at once (e.g. in a response message with a
     * repeated field), as the rectangle may cover a large area and contain a
     * huge number of features.
     * @param \Routeguide\Rectangle $request client request
     * @param \Grpc\ServerCallWriter $writer write response data of \Routeguide\Feature
     * @param \Grpc\ServerContext $context server request context
     * @return void
     */
    public function ListFeatures(
        \Routeguide\Rectangle $request,
        \Grpc\ServerCallWriter $writer,
        \Grpc\ServerContext $context
    ): void {
        $context->setStatus(\Grpc\Status::unimplemented());
        $writer->finish();
    }

    /**
     * A client-to-server streaming RPC.
     *
     * Accepts a stream of Points on a route being traversed, returning a
     * RouteSummary when traversal is completed.
     * @param \Grpc\ServerCallReader $reader read client request data of \Routeguide\Point
     * @param \Grpc\ServerContext $context server request context
     * @return \Routeguide\RouteSummary for response data, null if if error occured
     *     initial metadata (if any) and status (if not ok) should be set to $context
     */
    public function RecordRoute(
        \Grpc\ServerCallReader $reader,
        \Grpc\ServerContext $context
    ): ?\Routeguide\RouteSummary {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    /**
     * A Bidirectional streaming RPC.
     *
     * Accepts a stream of RouteNotes sent while a route is being traversed,
     * while receiving other RouteNotes (e.g. from other users).
     * @param \Grpc\ServerCallReader $reader read client request data of \Routeguide\RouteNote
     * @param \Grpc\ServerCallWriter $writer write response data of \Routeguide\RouteNote
     * @param \Grpc\ServerContext $context server request context
     * @return void
     */
    public function RouteChat(
        \Grpc\ServerCallReader $reader,
        \Grpc\ServerCallWriter $writer,
        \Grpc\ServerContext $context
    ): void {
        $context->setStatus(\Grpc\Status::unimplemented());
        $writer->finish();
    }

    /**
     * Get the method descriptors of the service for server registration
     *
     * @return array of \Grpc\MethodDescriptor for the service methods
     */
    public final function getMethodDescriptors(): array
    {
        return [
            '/routeguide.RouteGuide/GetFeature' => new \Grpc\MethodDescriptor(
                $this,
                'GetFeature',
                '\Routeguide\Point',
                \Grpc\MethodDescriptor::UNARY_CALL
            ),
            '/routeguide.RouteGuide/ListFeatures' => new \Grpc\MethodDescriptor(
                $this,
                'ListFeatures',
                '\Routeguide\Rectangle',
                \Grpc\MethodDescriptor::SERVER_STREAMING_CALL
            ),
            '/routeguide.RouteGuide/RecordRoute' => new \Grpc\MethodDescriptor(
                $this,
                'RecordRoute',
                '\Routeguide\Point',
                \Grpc\MethodDescriptor::CLIENT_STREAMING_CALL
            ),
            '/routeguide.RouteGuide/RouteChat' => new \Grpc\MethodDescriptor(
                $this,
                'RouteChat',
                '\Routeguide\RouteNote',
                \Grpc\MethodDescriptor::BIDI_STREAMING_CALL
            ),
        ];
    }

}
