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

    public function GetFeature(
        \Routeguide\Point $request,
        array $metadata,
        \Grpc\ServerContext $serverContext
    ) {
        foreach ($this->featureList as $feature) {
            if (
                $feature->getLocation()->getLatitude() === $request->getLatitude()
                && $feature->getLocation()->getLongitude() === $request->getLongitude()
            ) {
                return [$feature];
            }
        }
        $feature = new Routeguide\Feature([
            'name' => '',
            'location' => $request,
        ]);
        return [$feature];
    }

    private $featureList = [];
}
