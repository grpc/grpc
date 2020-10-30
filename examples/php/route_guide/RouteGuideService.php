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


class RouteGuide extends routeguide\RouteGuideServiceStub
{
    public function __construct($dbFilePath)
    {
        $dbFilePath = $dbFilePath ?? dirname(__FILE__) . '/route_guide_db.json';
        $dbData = file_get_contents($dbFilePath);
        if (!$dbData) {
            throw new InvalidArgumentException("Error reading route db file: " . $dbFilePath);
        }
        $featureList = json_decode($dbData);
        if (!$featureList) {
            throw new InvalidArgumentException("Error decoding route db file: " . $dbFilePath);
        }
        foreach ($featureList as $feature) {
            array_push($this->featureList, new Routeguide\Feature([
                'name' => $feature->name,
                'location' => new Routeguide\Point([
                    'latitude' => $feature->location->latitude,
                    'longitude' => $feature->location->longitude,
                ]),
            ]));
        }
    }

    private function findFeature(\Routeguide\Point $point)
    {
        foreach ($this->featureList as $feature) {
            if (
                $feature->getLocation()->getLatitude() === $point->getLatitude()
                && $feature->getLocation()->getLongitude() === $point->getLongitude()
            ) {
                return $feature;
            }
        }
        return null;
    }

    private function toRadians(float $num)
    {
        return $num * 3.1415926 / 180;
    }

    // The formula is based on http://mathforum.org/library/drmath/view/51879.html
    private function calculateDistance(\Routeguide\Point $start, \Routeguide\Point $end)
    {
        $toRadians = function (float $num) {
            return $num * 3.1415926 / 180;
        };
        $coordFactor = 10000000.0;
        $R = 6371000; // metres

        $lat_1 = $start->getLatitude() / $coordFactor;
        $lat_2 = $end->getLatitude() / $coordFactor;
        $lon_1 = $start->getLongitude() / $coordFactor;
        $lon_2 = $end->getLongitude() / $coordFactor;
        $lat_rad_1 = $toRadians($lat_1);
        $lat_rad_2 = $toRadians($lat_2);
        $delta_lat_rad = $toRadians($lat_2 - $lat_1);
        $delta_lon_rad = $toRadians($lon_2 - $lon_1);

        $a = pow(sin($delta_lat_rad / 2), 2) + cos($lat_rad_1) * cos($lat_rad_2) *
            pow(sin($delta_lon_rad / 2), 2);
        $c = 2 * atan2(sqrt($a), sqrt(1 - $a));

        return $R * $c;
    }

    public function GetFeature(
        \Routeguide\Point $request,
        array $metadata,
        \Grpc\ServerContext $serverContext
    ): array {
        $request = $serverContext->read();
        $feature = $this->findFeature($request);

        $notFoundFeature = new Routeguide\Feature([
            'name' => '',
            'location' => $request,
        ]);
        return [$feature ?? $notFoundFeature];
    }

    public function ListFeatures(
        \Routeguide\Rectangle $request,
        array $metadata,
        \Grpc\ServerContext $serverContext
    ) {
        $request = $serverContext->read();

        $lo = $request->getLo();
        $hi = $request->getHi();
        $left = min($lo->getLongitude(), $hi->getLongitude());
        $right = max($lo->getLongitude(), $hi->getLongitude());
        $top = max($lo->getLatitude(), $hi->getLatitude());
        $bottom = min($lo->getLatitude(), $hi->getLatitude());
        foreach ($this->featureList as $feature) {
            $longitude = $feature->getLocation()->getLongitude();
            $latitude = $feature->getLocation()->getLatitude();
            if (
                $longitude >= $left && $longitude <= $right
                && $latitude >= $bottom && $latitude <= $top
            ) {
                $serverContext->write($feature);
            }
        }
    }

    public function RecordRoute(
        \Routeguide\Point $request,
        array $metadata,
        \Grpc\ServerContext $serverContext
    ) {
        $point_count = 0;
        $feature_count = 0;
        $distance = 0;
        $previous = null;

        $start_time = time();
        while ($point = $serverContext->read()) {
            $point_count++;
            $feature = $this->findFeature($point);
            if ($feature) {
                $feature_count++;
                if ($previous) {
                    $distance += $this->calculateDistance($previous, $point);
                }
                $previous = $point;
            }
        }

        $summary = new \Routeguide\RouteSummary();
        $summary->setPointCount($point_count);
        $summary->setFeatureCount($feature_count);
        $summary->setDistance($distance);
        $summary->setElapsedTime(time() - $start_time);

        return [$summary];
    }

    public function RouteChat(
        \Routeguide\RouteNote $request,
        array $metadata,
        \Grpc\ServerContext $serverContext
    ) {
        while ($note = $serverContext->read()) {
            foreach ($this->received_notes as $n) {
                if (
                    $n->getLocation()->getLatitude() ===
                    $note->getLocation()->getLatitude()
                    && $n->getLocation()->getLongitude() ===
                    $note->getLocation()->getLongitude()
                ) {
                    $serverContext->write($n);
                }
            }
            array_push($this->received_notes, $note);
        }
    }

    private $received_notes = [];
    private $featureList = [];
}
