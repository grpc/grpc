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

    // [ <String method_full_path> => [
    //   'service' => <Object service>,
    //   'method'  => <String method_name>,
    //   'request' => <Object request>,
    // ] ]
    protected $paths_map;

    private function waitForNextEvent() {
        return $this->requestCall();
    }

    private function loadRequest($request, $call) {
        $event = $call->startBatch([
            OP_RECV_MESSAGE => true,
        ]);
        if (!$event->message) {
            throw new Exception("Did not receive a proper message");
        }
        $request->mergeFromString($event->message);
        return $request;
    }

    protected function sendResponse($context, $response, $initialMetadata, $status)
    {
        $batch = [
            OP_SEND_INITIAL_METADATA => $initialMetadata ?? [],
            OP_SEND_STATUS_FROM_SERVER => $status ?? Status::ok(),
            OP_RECV_CLOSE_ON_SERVER => true,
        ];
        if ($response) {
            $batch[OP_SEND_MESSAGE] = ['message' =>
            $response->serializeToString()];
        }
        $context->call()->startBatch($batch);
    }

    /**
     * Add a service to this server
     *
     * @param Object   $service      The service to be added
     */
    public function handle($service) {
        $rf = new \ReflectionClass($service);

        // If input does not have a parent class, which should be the
        // generated stub, don't proceeed. This might change in the
        // future.
        if (!$rf->getParentClass()) return;

        // The input class name needs to match the service name
        $service_name = $rf->getName();
        $namespace = $rf->getParentClass()->getNamespaceName();
        $prefix = "";
        if ($namespace) {
            $parts = explode("\\", $namespace);
            foreach ($parts as $part) {
                $prefix .= lcfirst($part) . ".";
            }
        }
        $base_path = "/" . $prefix . $service_name;

        // Right now, assume all the methods in the class are RPC method
        // implementations. Might change in the future.
        $methods = $rf->getParentClass()->getMethods();
        foreach ($methods as $method) {
            $method_name = $method->getName();
            $full_path = $base_path . "/" . ucfirst($method_name);

            $method_params = $method->getParameters();
            // RPC should have exactly 3 request param
            if (count($method_params) != 3) continue;
            $request_param = $method_params[0];
            // Method implementation must have type hint for request param
            if (!$request_param->getType()) continue;
            $request_type = $request_param->getType()->getName();

            // $full_path needs to match the incoming event->method
            // from requestCall() for us to know how to handle the request
            $this->paths_map[$full_path] = [
                'service' => $service,
                'method' => $method_name,
                'request' => new $request_type(),
            ];
        }
    }

    public function run()
    {
        $this->start();
        while (true) {
            // This blocks until the server receives a request
            $context = new ServerContextImpl($this->waitForNextEvent());
            if (!$context) {
                throw new Exception(
                    "Unexpected error: server->waitForNextEvent delivers"
                    . " an empty event"
                );
            }
            if (!$context->call()) {
                throw new Exception(
                    "Unexpected error: server->waitForNextEvent delivers"
                    . " an event without a call"
                );
            }
            $full_path = $context->method();

            // TODO: Can send a proper UNIMPLEMENTED response in the future
            if (!array_key_exists($full_path, $this->paths_map)) continue;

            $service = $this->paths_map[$full_path]['service'];
            $method = $this->paths_map[$full_path]['method'];
            $request = $this->paths_map[$full_path]['request'];

            $request = $this->loadRequest($request, $context->call());
            if (!$request) {
                throw new Exception("Unexpected error: fail to parse request");
            }
            if (!method_exists($service, $method)) {
                $this->sendResponse($context, null, [], Status::unimplemented());
            }

            // Dispatch to actual server logic
            list($response, $initialMetadata, $status) =
                $service->$method($request, $context->clientMetadata(), $context);
            $this->sendResponse($context, $response, $initialMetadata, $status);
        }
    }
}
