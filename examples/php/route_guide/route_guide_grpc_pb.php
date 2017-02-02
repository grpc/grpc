<?php
// GENERATED CODE -- DO NOT EDIT!

// Original file comments:
// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
namespace Routeguide {

  // Interface exported by the server.
  class RouteGuideClient extends \Grpc\BaseStub {

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param Grpc\Channel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null) {
      parent::__construct($hostname, $opts, $channel);
    }

    /**
     * A simple RPC.
     *
     * Obtains the feature at a given position.
     *
     * A feature with an empty name is returned if there's no feature at the given
     * position.
     * @param \Routeguide\Point $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function GetFeature(\Routeguide\Point $argument,
      $metadata = [], $options = []) {
      return $this->_simpleRequest('/routeguide.RouteGuide/GetFeature',
      $argument,
      ['\Routeguide\Feature', 'decode'],
      $metadata, $options);
    }

    /**
     * A server-to-client streaming RPC.
     *
     * Obtains the Features available within the given Rectangle.  Results are
     * streamed rather than returned at once (e.g. in a response message with a
     * repeated field), as the rectangle may cover a large area and contain a
     * huge number of features.
     * @param \Routeguide\Rectangle $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function ListFeatures(\Routeguide\Rectangle $argument,
      $metadata = [], $options = []) {
      return $this->_serverStreamRequest('/routeguide.RouteGuide/ListFeatures',
      $argument,
      ['\Routeguide\Feature', 'decode'],
      $metadata, $options);
    }

    /**
     * A client-to-server streaming RPC.
     *
     * Accepts a stream of Points on a route being traversed, returning a
     * RouteSummary when traversal is completed.
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function RecordRoute($metadata = [], $options = []) {
      return $this->_clientStreamRequest('/routeguide.RouteGuide/RecordRoute',
      ['\Routeguide\RouteSummary','decode'],
      $metadata, $options);
    }

    /**
     * A Bidirectional streaming RPC.
     *
     * Accepts a stream of RouteNotes sent while a route is being traversed,
     * while receiving other RouteNotes (e.g. from other users).
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function RouteChat($metadata = [], $options = []) {
      return $this->_bidiRequest('/routeguide.RouteGuide/RouteChat',
      ['\Routeguide\RouteNote','decode'],
      $metadata, $options);
    }

  }

}
