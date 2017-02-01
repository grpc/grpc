<?php
/*
 *
 * Copyright 2015, Google Inc.
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

require dirname(__FILE__).'/../vendor/autoload.php';

// The following includes are needed when using protobuf 3.1.0
// and will suppress warnings when using protobuf 3.2.0+
@include_once dirname(__FILE__).'/route_guide.pb.php';
@include_once dirname(__FILE__).'/route_guide_grpc_pb.php';

define('COORD_FACTOR', 1e7);

$client = new Routeguide\RouteGuideClient('localhost:50051', [
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
]);

function printFeature($feature)
{
    $name = $feature->getName();
    if (!$name) {
        $name_str = 'no feature';
    } else {
        $name_str = "feature called $name";
    }
    echo sprintf("Found %s \n  at %f, %f\n", $name_str,
                 $feature->getLocation()->getLatitude() / COORD_FACTOR,
                 $feature->getLocation()->getLongitude() / COORD_FACTOR);
}

/**
 * Run the getFeature demo. Calls getFeature with a point known to have a
 * feature and a point known not to have a feature.
 */
function runGetFeature()
{
    echo "Running GetFeature...\n";
    global $client;

    $point = new Routeguide\Point();
    $points = array(
        array(409146138, -746188906),
        array(0, 0),
    );

    foreach ($points as $p) {
        $point->setLatitude($p[0]);
        $point->setLongitude($p[1]);
        // make a unary grpc call
        list($feature, $status) = $client->GetFeature($point)->wait();
        printFeature($feature);
    }
}

/**
 * Run the listFeatures demo. Calls listFeatures with a rectangle
 * containing all of the features in the pre-generated
 * database. Prints each response as it comes in.
 */
function runListFeatures()
{
    echo "Running ListFeatures...\n";
    global $client;

    $lo_point = new Routeguide\Point();
    $hi_point = new Routeguide\Point();

    $lo_point->setLatitude(400000000);
    $lo_point->setLongitude(-750000000);
    $hi_point->setLatitude(420000000);
    $hi_point->setLongitude(-730000000);

    $rectangle = new Routeguide\Rectangle();
    $rectangle->setLo($lo_point);
    $rectangle->setHi($hi_point);

    // start the server streaming call
    $call = $client->ListFeatures($rectangle);
    // an iterator over the server streaming responses
    $features = $call->responses();
    foreach ($features as $feature) {
        printFeature($feature);
    }
}

/**
 * Run the recordRoute demo. Sends several randomly chosen points from the
 * pre-generated feature database with a variable delay in between. Prints
 * the statistics when they are sent from the server.
 */
function runRecordRoute()
{
    echo "Running RecordRoute...\n";
    global $client, $argv;

    // start the client streaming call
    $call = $client->RecordRoute();

    $db = json_decode(file_get_contents($argv[1]), true);
    $num_points_in_db = count($db);
    $num_points = 10;
    for ($i = 0; $i < $num_points; ++$i) {
        $point = new Routeguide\Point();
        $index = rand(0, $num_points_in_db - 1);
        $lat = $db[$index]['location']['latitude'];
        $long = $db[$index]['location']['longitude'];
        $feature_name = $db[$index]['name'];
        $point->setLatitude($lat);
        $point->setLongitude($long);
        echo sprintf("Visiting point %f, %f,\n  with feature name: %s\n",
                     $lat / COORD_FACTOR, $long / COORD_FACTOR,
                     $feature_name ? $feature_name : '<empty>');
        usleep(rand(300000, 800000));
        $call->write($point);
    }
    list($route_summary, $status) = $call->wait();
    echo sprintf("Finished trip with %d points\nPassed %d features\n".
                 "Travelled %d meters\nIt took %d seconds\n",
                 $route_summary->getPointCount(),
                 $route_summary->getFeatureCount(),
                 $route_summary->getDistance(),
                 $route_summary->getElapsedTime());
}

/**
 * Run the routeChat demo. Send some chat messages, and print any chat
 * messages that are sent from the server.
 */
function runRouteChat()
{
    echo "Running RouteChat...\n";
    global $client;

    // start the bidirectional streaming call
    $call = $client->RouteChat();

    $notes = array(
        array(1, 1, 'first message'),
        array(1, 2, 'second message'),
        array(2, 1, 'third message'),
        array(1, 1, 'fourth message'),
        array(1, 1, 'fifth message'),
    );

    foreach ($notes as $n) {
        $point = new Routeguide\Point();
        $point->setLatitude($lat = $n[0]);
        $point->setLongitude($long = $n[1]);

        $route_note = new Routeguide\RouteNote();
        $route_note->setLocation($point);
        $route_note->setMessage($message = $n[2]);

        echo sprintf("Sending message: '%s' at (%d, %d)\n",
                     $message, $lat, $long);
        // send a bunch of messages to the server
        $call->write($route_note);
    }
    $call->writesDone();

    // read from the server until there's no more
    while ($route_note_reply = $call->read()) {
        echo sprintf("Previous left message at (%d, %d): '%s'\n",
                     $route_note_reply->getLocation()->getLatitude(),
                     $route_note_reply->getLocation()->getLongitude(),
                     $route_note_reply->getMessage());
    }
}

/**
 * Run all of the demos in order.
 */
function main()
{
    runGetFeature();
    runListFeatures();
    runRecordRoute();
    runRouteChat();
}

if (empty($argv[1])) {
    echo 'Usage: php -d extension=grpc.so route_guide_client.php '.
        "<path to route_guide_db.json>\n";
    exit(1);
}
main();
