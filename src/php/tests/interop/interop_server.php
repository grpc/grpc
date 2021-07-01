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
require_once realpath(dirname(__FILE__) . '/../../vendor/autoload.php');

class TestService extends \Grpc\Testing\TestServiceStub
{
    private function maybeEchoMetadata(\Grpc\ServerContext $context)
    {
        $ECHO_INITIAL_KEY = 'x-grpc-test-echo-initial';
        $ECHO_TRAILING_KEY = 'x-grpc-test-echo-trailing-bin';

        $initial_metadata = [];
        $trailing_metadata = [];
        $client_metadata = $context->clientMetadata();
        if (array_key_exists($ECHO_INITIAL_KEY, $client_metadata)) {
            $initial_metadata = [
                $ECHO_INITIAL_KEY =>
                $client_metadata[$ECHO_INITIAL_KEY],
            ];
        }
        if (array_key_exists($ECHO_TRAILING_KEY, $client_metadata)) {
            $trailing_metadata = [
                $ECHO_TRAILING_KEY =>
                $client_metadata[$ECHO_TRAILING_KEY],
            ];
        }
        return [$initial_metadata, $trailing_metadata];
    }

    private function maybeEchoStatusAndMessage(
        $request,
        $trailing_metadata = []
    ) {
        if (!$request->hasResponseStatus()) {
            return null;
        }
        return \Grpc\Status::status(
            $request->getResponseStatus()->getCode(),
            $request->getResponseStatus()->getMessage(),
            $trailing_metadata
        );
    }

    public function EmptyCall(
        \Grpc\Testing\EmptyMessage $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\EmptyMessage {
        list($initial_metadata, $trailing_metadata) =
            $this->maybeEchoMetadata($context);
        $context->setStatus(\Grpc\Status::ok($trailing_metadata));
        $context->setInitialMetadata($initial_metadata);
        return new \Grpc\Testing\EmptyMessage();
    }

    public function UnaryCall(
        \Grpc\Testing\SimpleRequest $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\SimpleResponse {
        list($initial_metadata, $trailing_metadata) =
            $this->maybeEchoMetadata($context);
        $echo_status = $this->maybeEchoStatusAndMessage(
            $request,
            $trailing_metadata
        );

        $payload = new \Grpc\Testing\Payload([
            'type' => $request->getResponseType(),
            'body' => str_repeat("\0", $request->getResponseSize()),
        ]);
        $response = new \Grpc\Testing\SimpleResponse([
            'payload' => $payload,
        ]);

        $context->setInitialMetadata($initial_metadata);
        $context->setStatus($echo_status ?? \Grpc\Status::ok($trailing_metadata));
        return  $response;
    }

    public function CacheableUnaryCall(
        \Grpc\Testing\SimpleRequest $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\SimpleResponse {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }

    public function StreamingOutputCall(
        \Grpc\Testing\StreamingOutputCallRequest $request,
        \Grpc\ServerCallWriter $writter,
        \Grpc\ServerContext $context
    ): void {
        $echo_status = $this->maybeEchoStatusAndMessage($request);

        foreach ($request->getResponseParameters() as $parameter) {
            if ($parameter->getIntervalUs() > 0) {
                usleep($parameter->getIntervalUs());
            }
            $payload = new \Grpc\Testing\Payload([
                'type' => $request->getResponseType(),
                'body' => str_repeat("\0", $parameter->getSize()),
            ]);
            $response = new \Grpc\Testing\StreamingOutputCallResponse([
                'payload' => $payload,
            ]);
            $options = [];
            $writter->write($response, $options);
        }
        $context->setStatus($echo_status ?? \Grpc\Status::ok());
        $writter->finish();
    }

    public function StreamingInputCall(
        \Grpc\ServerCallReader $reader,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\StreamingInputCallResponse {
        $aggregate_size = 0;
        while ($request = $reader->read()) {
            if ($request->hasPayload()) {
                $aggregate_size += strlen($request->getPayload()->getBody());
            }
        }
        $response = new \Grpc\Testing\StreamingInputCallResponse();
        $response->setAggregatedPayloadSize($aggregate_size);
        return $response;
    }

    public function FullDuplexCall(
        \Grpc\ServerCallReader $reader,
        \Grpc\ServerCallWriter $writter,
        \Grpc\ServerContext $context
    ): void {
        list($initial_metadata, $trailing_metadata) =
            $this->maybeEchoMetadata($context);
        $context->setInitialMetadata($initial_metadata);
        while ($request = $reader->read()) {
            $echo_status = $this->maybeEchoStatusAndMessage(
                $request,
                $trailing_metadata
            );
            if ($echo_status) {
                $context->setStatus($echo_status);
                $writter->finish();
                return;
            }

            foreach ($request->getResponseParameters() as $parameter) {
                if ($parameter->getIntervalUs() > 0) {
                    usleep($parameter->getIntervalUs());
                }
                $payload = new \Grpc\Testing\Payload([
                    'type' => $request->getResponseType(),
                    'body' => str_repeat("\0", $parameter->getSize()),
                ]);
                $response = new \Grpc\Testing\StreamingOutputCallResponse([
                    'payload' => $payload,
                ]);
                $options = [];
                $writter->write($response, $options);
            }
        }
        $context->setStatus(\Grpc\Status::ok($trailing_metadata));
        $writter->finish();
    }

    public function HalfDuplexCall(
        \Grpc\ServerCallReader $reader,
        \Grpc\ServerCallWriter $writter,
        \Grpc\ServerContext $context
    ): void {
        $context->setStatus(\Grpc\Status::unimplemented());
        $writter->finish();
    }

    public function UnimplementedCall(
        \Grpc\Testing\EmptyMessage $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\EmptyMessage {
        $context->setStatus(\Grpc\Status::unimplemented());
        return null;
    }
};


$args = getopt('', ['port:', 'use_tls::',]);

$server = new \Grpc\RpcServer();

$listening_address = '0.0.0.0:' . $args['port'];
if ($args['use_tls']) {
    $server_credentials = \Grpc\ServerCredentials::createSsl(
        null,
        file_get_contents(dirname(__FILE__) . '/../data/server1.key'),
        file_get_contents(dirname(__FILE__) . '/../data/server1.pem')
    );
    $server->addSecureHttp2Port($listening_address, $server_credentials);
} else {
    $server->addHttp2Port($listening_address);
}
$server->handle(new TestService());
echo 'Server running on ' . $listening_address . PHP_EOL;
$server->run();
