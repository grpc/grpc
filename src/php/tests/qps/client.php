<?php
/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * PHP client for QPS testing works as follows:
 * 1. Gets initiated by a call from a proxy that implements the worker service. The
 *    argument to this client is the proxy connection information
 * 2. Initiate an RPC to the proxy to get ClientConfig
 * 3. Initiate a client-side telemetry RPC to the proxy
 * 4. Parse the client config, which includes server target information and then start
 *    a unary or streaming test as appropriate.
 * 5. After each completed RPC, send its timing to the proxy. The proxy does all histogramming
 * 6. Proxy will respond on the timing channel when it's time to complete. Our
 *    next timing write will fail and we know that it's time to stop
 * The above complex dance is since threading and async are not idiomatic and we
 * shouldn't ever be waiting to read a mark
 *
 * This test only supports a single channel since threading/async is not idiomatic
 * This test supports unary or streaming ping-pongs, as well as open-loop
 * 
 */

require dirname(__FILE__).'/vendor/autoload.php';

/**
 * Assertion function that always exits with an error code if the assertion is
 * falsy.
 *
 * @param $value Assertion value. Should be true.
 * @param $error_message Message to display if the assertion is false
 */
function hardAssert($value, $error_message)
{
    if (!$value) {
        echo $error_message."\n";
        exit(1);
    }
}

function hardAssertIfStatusOk($status)
{
    if ($status->code !== Grpc\STATUS_OK) {
        echo "Call did not complete successfully. Status object:\n";
        var_dump($status);
        exit(1);
    }
}

/* Start the actual client */

function qps_client_main($proxy_address) {
    echo "Initiating php client\n";

    $proxystubopts = [];
    $proxystubopts['credentials'] = Grpc\ChannelCredentials::createInsecure();
    $proxystub = new Grpc\Testing\ProxyClientServiceClient($proxy_address, $proxystubopts);
    list($config, $status) = $proxystub->GetConfig(new Grpc\Testing\Void())->wait();
    hardAssertIfStatusOk($status);
    hardAssert($config->getClientChannels() == 1, "Only 1 channel supported");
    hardAssert($config->getOutstandingRpcsPerChannel() == 1, "Only 1 outstanding RPC supported");

    echo "Got configuration from proxy, target is " . $config->getServerTargets()[0] . "\n";

    $stubopts = [];
    if ($config->getSecurityParams()) {
        if ($config->getSecurityParams()->getUseTestCa()) {
            $stubopts['credentials'] = Grpc\ChannelCredentials::createSsl(
                file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
        } else {
            $stubopts['credentials'] = Grpc\ChannelCredentials::createSsl(null);
        }
        $override = $config->getSecurityParams()->getServerHostOverride();
        if ($override) {
            $stubopts['grpc.ssl_target_name_override'] = $override;
            $stubopts['grpc.default_authority'] = $override;
        }
    } else {
        $stubopts['credentials'] = Grpc\ChannelCredentials::createInsecure();
    }
    echo "Initiating php benchmarking client\n";

    $stub = new Grpc\Testing\BenchmarkServiceClient(
        $config->getServerTargets()[0], $stubopts);
    $req = new Grpc\Testing\SimpleRequest();

    $req->setResponseType(Grpc\Testing\PayloadType::COMPRESSABLE);
    $req->setResponseSize($config->getPayloadConfig()->getSimpleParams()->getRespSize());
    $payload = new Grpc\Testing\Payload();
    $payload->setType(Grpc\Testing\PayloadType::COMPRESSABLE);
    $payload->setBody(str_repeat("\0", $config->getPayloadConfig()->getSimpleParams()->getReqSize()));
    $req->setPayload($payload);

    /* TODO(stanley-cheung): Enable the following by removing the 0&& once protobuf
     * properly supports oneof in PHP */
    if (0 && $config->getLoadParams()->getLoad() == "poisson") {
        $poisson = true;
        $lamrecip = 1.0/($config->getLoadParams()->getPoisson()->getOfferedLoad());
        $issue = microtime(true) + $lamrecip * -log(1.0-rand()/(getrandmax()+1));
    } else {
        $poisson = false;
    }
    $metric = new Grpc\Testing\ProxyStat;
    $telemetry = $proxystub->ReportTime();
    if ($config->getRpcType() == Grpc\Testing\RpcType::UNARY) {
        while (1) {
            if ($poisson) {
                time_sleep_until($issue);
                $issue = $issue + $lamrecip * -log(1.0-rand()/(getrandmax()+1));
            }
            $startreq = microtime(true);
            list($resp,$status) = $stub->UnaryCall($req)->wait();
            hardAssertIfStatusOk($status);
            $metric->setLatency(microtime(true)-$startreq);
            $telemetry->write($metric);
        }
    } else {
        $stream = $stub->StreamingCall();
        while (1) {
            if ($poisson) {
                time_sleep_until($issue);
                $issue = $issue + $lamrecip * -log(1.0-rand()/(getrandmax()+1));
            }
            $startreq = microtime(true);
            $stream->write($req);
            $resp = $stream->read();
            $metric->setLatency(microtime(true)-$startreq);
            $telemetry->write($metric);
        }
    }
}

ini_set('display_startup_errors', 1);
ini_set('display_errors', 1);
error_reporting(-1);
qps_client_main($argv[1]);
