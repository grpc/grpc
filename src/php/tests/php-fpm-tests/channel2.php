<?php

$channel = new Grpc\Channel('localhost:50002',
    ['credentials' => Grpc\ChannelCredentials::createInsecure()]);

$plist = $channel->getPersistentList();

// The persistent list should have 'localhost:50001' created by
// the previews request.
$return_status = 1;
foreach ($plist as &$value) {
    if ($value["target"] == "localhost:50001") {
        $return_status = 0;
        break;
    }
}
echo $return_status. PHP_EOL;
