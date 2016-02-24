<?php

$channel = new Grpc\Channel('localhost:1000', [
    'credentials' => Grpc\ChannelCredentials::createInsecure()
]);

$deadline = Grpc\Timeval::infFuture();
$call = new Grpc\Call($channel,
                      'dummy_method',
                      $deadline);

$call->cancel();
$channel->close();
