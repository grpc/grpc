--TEST--
Test watchConnectivityState of Channel : basic functionality
--FILE--
<?php
$channel = new Grpc\Channel('localhost:0', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
$time = new Grpc\Timeval(1000);
$state = $channel->watchConnectivityState(123, $time);
unset($time);
var_dump($state);
?>
===DONE===
--EXPECTF--
%s
bool(true)
===DONE===
