--TEST--
Test getConnectivityState of Channel : basic functionality
--FILE--
<?php
$channel = new Grpc\Channel('localhost:0', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
$state = $channel->getConnectivityState(true);
var_dump($state);
?>
===DONE===
--EXPECTF--
%s
int(0)
===DONE===
