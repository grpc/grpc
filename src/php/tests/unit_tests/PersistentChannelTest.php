<?php
/*
 *
 * Copyright 2015 gRPC authors.
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

class PersistentListTest extends PHPUnit_Framework_TestCase
{
  public function setUp()
  {
  }

  public function tearDown()
  {
      $channel_clean_persistent =
          new Grpc\Channel('localhost:50010', []);
      $channel_clean_persistent->cleanPersistentList();
  }

  public function waitUntilNotIdle($channel) {
      for ($i = 0; $i < 10; $i++) {
          $now = Grpc\Timeval::now();
          $deadline = $now->add(new Grpc\Timeval(1000));
          if ($channel->watchConnectivityState(GRPC\CHANNEL_IDLE,
            $deadline)) {
              return true;
          }
      }
      $this->assertTrue(false);
  }

  public function assertConnecting($state) {
      $this->assertTrue($state == GRPC\CHANNEL_CONNECTING ||
      $state == GRPC\CHANNEL_TRANSIENT_FAILURE);
  }

  public function testPersistentChennelCreateOneChannel()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', []);
      $plist = $this->channel1->getPersistentList();
      $this->assertEquals($plist['localhost:1']['target'], 'localhost:1');
      $this->assertArrayHasKey('localhost:1', $plist);
      $this->assertEquals($plist['localhost:1']['ref_count'], 1);
      $this->assertEquals($plist['localhost:1']['connectivity_status'],
                          GRPC\CHANNEL_IDLE);
      $this->assertEquals($plist['localhost:1']['is_valid'], 1);
      $this->channel1->close();
  }

  public function testPersistentChennelStatusChange()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', []);
      $plist = $this->channel1->getPersistentList();
      $this->assertEquals($plist['localhost:1']['connectivity_status'],
                          GRPC\CHANNEL_IDLE);
      $this->assertEquals($plist['localhost:1']['is_valid'], 1);
      $state = $this->channel1->getConnectivityState(true);

      $this->waitUntilNotIdle($this->channel1);
      $plist = $this->channel1->getPersistentList();
      $this->assertConnecting($plist['localhost:1']['connectivity_status']);
      $this->assertEquals($plist['localhost:1']['is_valid'], 1);

      $this->channel1->close();
  }

  public function testPersistentChennelCloseChannel()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', []);
      $plist = $this->channel1->getPersistentList();
      $this->assertEquals($plist['localhost:1']['ref_count'], 1);
      $this->channel1->close();
      $plist = $this->channel1->getPersistentList();
      $this->assertArrayNotHasKey('localhost:1', $plist);
  }

  public function testPersistentChannelSameHost()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', []);
      $this->channel2 = new Grpc\Channel('localhost:1', []);
      //ref_count should be 2
      $plist = $this->channel1->getPersistentList();
      $this->assertArrayHasKey('localhost:1', $plist);
      $this->assertEquals($plist['localhost:1']['ref_count'], 2);
      $this->channel1->close();
      $this->channel2->close();
  }

  public function testPersistentChannelDifferentHost()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', []);
      $this->channel2 = new Grpc\Channel('localhost:2', []);
      $plist = $this->channel1->getPersistentList();
      $this->assertArrayHasKey('localhost:1', $plist);
      $this->assertArrayHasKey('localhost:2', $plist);
      $this->assertEquals($plist['localhost:1']['ref_count'], 1);
      $this->assertEquals($plist['localhost:2']['ref_count'], 1);
      $this->channel1->close();
      $this->channel2->close();
  }

}
