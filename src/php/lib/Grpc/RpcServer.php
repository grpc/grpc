<?php
/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

namespace Grpc;

/**
 * This is an experimental and incomplete implementation of gRPC server
 * for PHP. APIs are _definitely_ going to be changed.
 *
 * DO NOT USE in production.
 */

/**
 * Class RpcServer
 * @package Grpc
 */
class RpcServer extends Server
{
    // [ <String method_full_path> => MethodDescriptor ]
    private $paths_map = [];

    private function waitForNextEvent()
    {
        return $this->requestCall();
    }

    /**
     * Add a service to this server
     *
     * @param Object   $service      The service to be added
     */
    public function handle($service)
    {
        $methodDescriptors = $service->getMethodDescriptors();
        $exist_methods = array_intersect_key($this->paths_map, $methodDescriptors);
        if (!empty($exist_methods)) {
            fwrite(STDERR, "WARNING: " . 'override already registered methods: ' .
                implode(', ', array_keys($exist_methods)) . PHP_EOL);
        }

        $this->paths_map = array_merge($this->paths_map, $methodDescriptors);
        return $this->paths_map;
    }

    public function run()
    {
        $this->start();
        while (true) try {
            // This blocks until the server receives a request
            $event = $this->waitForNextEvent();

            $full_path = $event->method;
            $context = new ServerContext($event);
            $server_writer = new ServerCallWriter($event->call, $context);

            if (!array_key_exists($full_path, $this->paths_map)) {
                $context->setStatus(Status::unimplemented());
                $server_writer->finish();
                continue;
            };

            $method_desc = $this->paths_map[$full_path];
            $server_reader = new ServerCallReader(
                $event->call,
                $method_desc->request_type
            );

            try {
                $this->processCall(
                    $method_desc,
                    $server_reader,
                    $server_writer,
                    $context
                );
            } catch (\Exception $e) {
                $context->setStatus(Status::status(
                    STATUS_INTERNAL,
                    $e->getMessage()
                ));
                $server_writer->finish();
            }
        } catch (\Exception $e) {
            fwrite(STDERR, "ERROR: " . $e->getMessage() . PHP_EOL);
            exit(1);
        }
    }

    private function processCall(
        MethodDescriptor $method_desc,
        ServerCallReader $server_reader,
        ServerCallWriter $server_writer,
        ServerContext $context
    ) {
        // Dispatch to actual server logic
        switch ($method_desc->call_type) {
            case MethodDescriptor::UNARY_CALL:
                $request = $server_reader->read();
                $response =
                    call_user_func(
                        array($method_desc->service, $method_desc->method_name),
                        $request ?? new $method_desc->request_type,
                        $context
                    );
                $server_writer->finish($response);
                break;
            case MethodDescriptor::SERVER_STREAMING_CALL:
                $request = $server_reader->read();
                call_user_func(
                    array($method_desc->service, $method_desc->method_name),
                    $request ?? new $method_desc->request_type,
                    $server_writer,
                    $context
                );
                break;
            case MethodDescriptor::CLIENT_STREAMING_CALL:
                $response = call_user_func(
                    array($method_desc->service, $method_desc->method_name),
                    $server_reader,
                    $context
                );
                $server_writer->finish($response);
                break;
            case MethodDescriptor::BIDI_STREAMING_CALL:
                call_user_func(
                    array($method_desc->service, $method_desc->method_name),
                    $server_reader,
                    $server_writer,
                    $context
                );
                break;
            default:
                throw new \Exception();
        }
    }
}
