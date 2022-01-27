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

require dirname(__FILE__) . '/vendor/autoload.php';
require dirname(__FILE__) . '/route_guide/RouteGuideService.php';

class Greeter extends \Helloworld\GreeterStub
{
    public function SayHello(
        \Helloworld\HelloRequest $request,
        \Grpc\ServerContext $serverContext
    ): ?\Helloworld\HelloReply {
        $name = $request->getName();
        $response = new \Helloworld\HelloReply();
        $response->setMessage("Hello " . $name);
        return $response;
    }
}


$server = new \Grpc\RpcServer();
$server->addHttp2Port('0.0.0.0:50051');
$server->handle(new RouteGuideService(null));
$server->handle(new Greeter());
$server->run();
