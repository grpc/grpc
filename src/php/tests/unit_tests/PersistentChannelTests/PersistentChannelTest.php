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

/**
 * @group persistent_list_bound_tests
 */
class PersistentListTest extends \PHPUnit\Framework\TestCase
{
  public function setUp(): void
  {
  }

  public function tearDown(): void
  {
    $channel_clean_persistent =
        new Grpc\Channel('localhost:50010', []);
    $plist = $channel_clean_persistent->getPersistentList();
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

  public function testInitHelper()
  {
      // PersistentList is not empty at the beginning of the tests
      // because phpunit will cache the channels created by other test
      // files.
  }


  public function testChannelNotPersist()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', ['force_new' => true]);
      $channel1_info = $this->channel1->getChannelInfo();
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals($channel1_info['target'], 'localhost:1');
      $this->assertEquals($channel1_info['ref_count'], 1);
      $this->assertEquals($channel1_info['connectivity_status'],
          GRPC\CHANNEL_IDLE);
      $this->assertEquals(count($plist_info), 0);
      $this->channel1->close();
  }

  public function testPersistentChannelCreateOneChannel()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', []);
      $channel1_info = $this->channel1->getChannelInfo();
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals($channel1_info['target'], 'localhost:1');
      $this->assertEquals($channel1_info['ref_count'], 2);
      $this->assertEquals($channel1_info['connectivity_status'],
                          GRPC\CHANNEL_IDLE);
      $this->assertArrayHasKey($channel1_info['key'], $plist_info);
      $this->assertEquals(count($plist_info), 1);
      $this->channel1->close();
  }

  public function testPersistentChannelCreateMultipleChannels()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', []);
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(count($plist_info), 1);

      $this->channel2 = new Grpc\Channel('localhost:2', []);
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(count($plist_info), 2);

      $this->channel3 = new Grpc\Channel('localhost:3', []);
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(count($plist_info), 3);
  }

  public function testPersistentChannelStatusChange()
  {
      $this->channel1 = new Grpc\Channel('localhost:4', []);
      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertEquals($channel1_info['connectivity_status'],
                          GRPC\CHANNEL_IDLE);

      $this->channel1->getConnectivityState(true);
      $this->waitUntilNotIdle($this->channel1);
      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertConnecting($channel1_info['connectivity_status']);
      $this->channel1->close();
  }

  public function testPersistentChannelCloseChannel()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', []);
      $this->channel2 = new Grpc\Channel('localhost:1', []);

      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertEquals($channel1_info['ref_count'], 3);
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals($plist_info[$channel1_info['key']]['ref_count'], 3);

      $this->channel1->close();
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals($plist_info[$channel1_info['key']]['ref_count'], 2);

      $this->channel2->close();
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals($plist_info[$channel1_info['key']]['ref_count'], 1);
  }

  public function testPersistentChannelSameTarget()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', []);
      $this->channel2 = new Grpc\Channel('localhost:1', []);
      $plist = $this->channel2->getPersistentList();
      $channel1_info = $this->channel1->getChannelInfo();
      $channel2_info = $this->channel2->getChannelInfo();
      // $channel1 and $channel2 shares the same channel, thus only 1
      // channel should be in the persistent list.
      $this->assertEquals($channel1_info['key'], $channel2_info['key']);
      $this->assertArrayHasKey($channel1_info['key'], $plist);
      $this->assertEquals(count($plist), 1);
      $this->channel1->close();
      $this->channel2->close();
  }

  public function testPersistentChannelDifferentTarget()
  {
      $this->channel1 = new Grpc\Channel('localhost:1', []);
      $channel1_info = $this->channel1->getChannelInfo();
      $this->channel2 = new Grpc\Channel('localhost:2', []);
      $channel2_info = $this->channel1->getChannelInfo();
      $plist_info = $this->channel1->getPersistentList();
      $this->assertArrayHasKey($channel1_info['key'], $plist_info);
      $this->assertArrayHasKey($channel2_info['key'], $plist_info);
      $this->assertEquals($plist_info[$channel1_info['key']]['ref_count'], 2);
      $this->assertEquals($plist_info[$channel2_info['key']]['ref_count'], 2);
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(count($plist_info), 2);
      $this->channel1->close();
      $this->channel2->close();
  }

  public function testPersistentChannelSharedChannelClose()
  {
    $this->expectException(\RuntimeException::class);
    $this->expectExceptionMessage("startBatch Error. Channel is closed");
    // same underlying channel
      $this->channel1 = new Grpc\Channel('localhost:10001', [
          "grpc_target_persist_bound" => 2,
      ]);
      $this->channel2 = new Grpc\Channel('localhost:10001', []);
      $this->server = new Grpc\Server([]);
      $this->port = $this->server->addHttp2Port('localhost:10001');
      $this->server->start();

      // channel2 can still be use
      $state = $this->channel2->getConnectivityState();
      $this->assertEquals(GRPC\CHANNEL_IDLE, $state);

      $call1 = new Grpc\Call($this->channel1,
          '/foo',
          Grpc\Timeval::infFuture());
      $call2 = new Grpc\Call($this->channel2,
          '/foo',
          Grpc\Timeval::infFuture());
      $call3 = new Grpc\Call($this->channel1,
          '/foo',
          Grpc\Timeval::infFuture());
      $call4 = new Grpc\Call($this->channel2,
          '/foo',
          Grpc\Timeval::infFuture());
      $batch = [
          Grpc\OP_SEND_INITIAL_METADATA => [],
      ];

      $result = $call1->startBatch($batch);
      $this->assertTrue($result->send_metadata);
      $result = $call2->startBatch($batch);
      $this->assertTrue($result->send_metadata);

      $this->channel1->close();
      // After closing channel1, channel2 can still be use
      $result = $call4->startBatch($batch);
      $this->assertTrue($result->send_metadata);
      // channel 1 is closed, it will throw an exception.
      $result = $call3->startBatch($batch);
  }

  public function testPersistentChannelTargetDefaultUpperBound()
  {
      $this->channel1 = new Grpc\Channel('localhost:10002', []);
      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertEquals($channel1_info['target_upper_bound'], 1);
      $this->assertEquals($channel1_info['target_current_size'], 1);
  }

  public function testPersistentChannelTargetUpperBoundZero()
  {
      $this->channel1 = new Grpc\Channel('localhost:10002', [
          "grpc_target_persist_bound" => 0,
      ]);
      // channel1 will not be persisted.
      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertEquals($channel1_info['target_upper_bound'], 0);
      $this->assertEquals($channel1_info['target_current_size'], 0);
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(0, count($plist_info));
  }

  public function testPersistentChannelTargetUpperBoundNotZero()
  {
      $this->channel1 = new Grpc\Channel('localhost:10003', [
          "grpc_target_persist_bound" => 3,
      ]);
      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertEquals($channel1_info['target_upper_bound'], 3);
      $this->assertEquals($channel1_info['target_current_size'], 1);

      // The upper bound should not be changed
      $this->channel2 = new Grpc\Channel('localhost:10003', []);
      $channel2_info = $this->channel2->getChannelInfo();
      $this->assertEquals($channel2_info['target_upper_bound'], 3);
      $this->assertEquals($channel2_info['target_current_size'], 1);

      // The upper bound should not be changed
      $channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
          null);
      $this->channel3 = new Grpc\Channel('localhost:10003',
          ['credentials' => $channel_credentials]);
      $channel3_info = $this->channel3->getChannelInfo();
      $this->assertEquals($channel3_info['target_upper_bound'], 3);
      $this->assertEquals($channel3_info['target_current_size'], 2);

      // The upper bound should not be changed
      $this->channel4 = new Grpc\Channel('localhost:10003', [
          "grpc_target_persist_bound" => 5,
      ]);
      $channel4_info = $this->channel4->getChannelInfo();
      $this->assertEquals($channel4_info['target_upper_bound'], 5);
      $this->assertEquals($channel4_info['target_current_size'], 2);
  }

  public function testPersistentChannelDefaultOutBound1()
  {
      $this->channel1 = new Grpc\Channel('localhost:10004', []);
      // Make channel1 not IDLE.
      $this->channel1->getConnectivityState(true);
      $this->waitUntilNotIdle($this->channel1);
      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertConnecting($channel1_info['connectivity_status']);

      // Since channel1 is CONNECTING, channel 2 will not be persisted
      $channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
        null);
      $this->channel2 = new Grpc\Channel('localhost:10004',
          ['credentials' => $channel_credentials]);
      $channel2_info = $this->channel2->getChannelInfo();
      $this->assertEquals(GRPC\CHANNEL_IDLE, $channel2_info['connectivity_status']);

      // By default, target 'localhost:10011' only persist one channel.
      // Since channel1 is not Idle channel2 will not be persisted.
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(1, count($plist_info));
      $this->assertArrayHasKey($channel1_info['key'], $plist_info);
      $this->assertArrayNotHasKey($channel2_info['key'], $plist_info);
  }

  public function testPersistentChannelDefaultOutBound2()
  {
      $this->channel1 = new Grpc\Channel('localhost:10005', []);
      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertEquals(GRPC\CHANNEL_IDLE, $channel1_info['connectivity_status']);

      // Although channel1 is IDLE, channel1 still has reference to the underline
      // gRPC channel. channel2 will not be persisted
      $channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
        null);
      $this->channel2 = new Grpc\Channel('localhost:10005',
          ['credentials' => $channel_credentials]);
      $channel2_info = $this->channel2->getChannelInfo();
      $this->assertEquals(GRPC\CHANNEL_IDLE, $channel2_info['connectivity_status']);

      // By default, target 'localhost:10011' only persist one channel.
      // Since channel1 Idle, channel2 will be persisted.
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(1, count($plist_info));
      $this->assertArrayHasKey($channel1_info['key'], $plist_info);
      $this->assertArrayNotHasKey($channel2_info['key'], $plist_info);
  }

  public function testPersistentChannelDefaultOutBound3()
  {
      $this->channel1 = new Grpc\Channel('localhost:10006', []);
      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertEquals(GRPC\CHANNEL_IDLE, $channel1_info['connectivity_status']);

      $this->channel1->close();
      // channel1 is closed, no reference holds to the underline channel.
      // channel2 can be persisted.
      $channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
        null);
      $this->channel2 = new Grpc\Channel('localhost:10006',
        ['credentials' => $channel_credentials]);
      $channel2_info = $this->channel2->getChannelInfo();
      $this->assertEquals(GRPC\CHANNEL_IDLE, $channel2_info['connectivity_status']);

      // By default, target 'localhost:10011' only persist one channel.
      // Since channel1 Idle, channel2 will be persisted.
      $plist_info = $this->channel2->getPersistentList();
      $this->assertEquals(1, count($plist_info));
      $this->assertArrayHasKey($channel2_info['key'], $plist_info);
      $this->assertArrayNotHasKey($channel1_info['key'], $plist_info);
  }

  public function testPersistentChannelTwoUpperBound()
  {
      $this->channel1 = new Grpc\Channel('localhost:10007', [
          "grpc_target_persist_bound" => 2,
      ]);
      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertEquals(GRPC\CHANNEL_IDLE, $channel1_info['connectivity_status']);

      // Since channel1 is IDLE, channel 1 will be deleted
      $channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
          null);
      $this->channel2 = new Grpc\Channel('localhost:10007',
          ['credentials' => $channel_credentials]);
      $channel2_info = $this->channel2->getChannelInfo();
      $this->assertEquals(GRPC\CHANNEL_IDLE, $channel2_info['connectivity_status']);

      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(2, count($plist_info));
      $this->assertArrayHasKey($channel1_info['key'], $plist_info);
      $this->assertArrayHasKey($channel2_info['key'], $plist_info);
  }

  public function testPersistentChannelTwoUpperBoundOutBound1()
  {
      $this->channel1 = new Grpc\Channel('localhost:10011', [
          "grpc_target_persist_bound" => 2,
      ]);
      $channel1_info = $this->channel1->getChannelInfo();

      $channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
        null);
      $this->channel2 = new Grpc\Channel('localhost:10011',
          ['credentials' => $channel_credentials]);
      $channel2_info = $this->channel2->getChannelInfo();

      // Close channel1, so that new channel can be persisted.
      $this->channel1->close();

      $channel_credentials = Grpc\ChannelCredentials::createSsl("a", null,
        null);
      $this->channel3 = new Grpc\Channel('localhost:10011',
          ['credentials' => $channel_credentials]);
      $channel3_info = $this->channel3->getChannelInfo();

      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(2, count($plist_info));
      $this->assertArrayNotHasKey($channel1_info['key'], $plist_info);
      $this->assertArrayHasKey($channel2_info['key'], $plist_info);
      $this->assertArrayHasKey($channel3_info['key'], $plist_info);
  }

  public function testPersistentChannelTwoUpperBoundOutBound2()
  {
      $this->channel1 = new Grpc\Channel('localhost:10012', [
          "grpc_target_persist_bound" => 2,
      ]);
      $channel1_info = $this->channel1->getChannelInfo();

      $channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
        null);
      $this->channel2 = new Grpc\Channel('localhost:10012',
        ['credentials' => $channel_credentials]);
      $channel2_info = $this->channel2->getChannelInfo();

      // Close channel2, so that new channel can be persisted.
      $this->channel2->close();

      $channel_credentials = Grpc\ChannelCredentials::createSsl("a", null,
        null);
      $this->channel3 = new Grpc\Channel('localhost:10012',
        ['credentials' => $channel_credentials]);
      $channel3_info = $this->channel3->getChannelInfo();

      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(2, count($plist_info));
      $this->assertArrayHasKey($channel1_info['key'], $plist_info);
      $this->assertArrayNotHasKey($channel2_info['key'], $plist_info);
      $this->assertArrayHasKey($channel3_info['key'], $plist_info);
  }

  public function testPersistentChannelTwoUpperBoundOutBound3()
  {
      $this->channel1 = new Grpc\Channel('localhost:10013', [
          "grpc_target_persist_bound" => 2,
      ]);
      $channel1_info = $this->channel1->getChannelInfo();

      $channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
          null);
      $this->channel2 = new Grpc\Channel('localhost:10013',
          ['credentials' => $channel_credentials]);
      $this->channel2->getConnectivityState(true);
      $this->waitUntilNotIdle($this->channel2);
      $channel2_info = $this->channel2->getChannelInfo();
      $this->assertConnecting($channel2_info['connectivity_status']);

      // Only one channel will be deleted
      $this->channel1->close();
      $this->channel2->close();

      $channel_credentials = Grpc\ChannelCredentials::createSsl("a", null,
        null);
      $this->channel3 = new Grpc\Channel('localhost:10013',
          ['credentials' => $channel_credentials]);
      $channel3_info = $this->channel3->getChannelInfo();

      // Only the Idle Channel will be deleted
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(2, count($plist_info));
      $this->assertArrayNotHasKey($channel1_info['key'], $plist_info);
      $this->assertArrayHasKey($channel2_info['key'], $plist_info);
      $this->assertArrayHasKey($channel3_info['key'], $plist_info);
  }

  public function testPersistentChannelTwoUpperBoundOutBound4()
  {
      $this->channel1 = new Grpc\Channel('localhost:10014', [
          "grpc_target_persist_bound" => 2,
      ]);
      $this->channel1->getConnectivityState(true);
      $this->waitUntilNotIdle($this->channel1);
      $channel1_info = $this->channel1->getChannelInfo();
      $this->assertConnecting($channel1_info['connectivity_status']);

      $channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
          null);
      $this->channel2 = new Grpc\Channel('localhost:10014',
          ['credentials' => $channel_credentials]);
      $this->channel2->getConnectivityState(true);
      $this->waitUntilNotIdle($this->channel2);
      $channel2_info = $this->channel2->getChannelInfo();
      $this->assertConnecting($channel2_info['connectivity_status']);

      $channel_credentials = Grpc\ChannelCredentials::createSsl("a", null,
          null);
      $this->channel3 = new Grpc\Channel('localhost:10014',
          ['credentials' => $channel_credentials]);
      $channel3_info = $this->channel3->getChannelInfo();

      // Channel3 will not be persisted
      $plist_info = $this->channel1->getPersistentList();
      $this->assertEquals(2, count($plist_info));
      $this->assertArrayHasKey($channel1_info['key'], $plist_info);
      $this->assertArrayHasKey($channel2_info['key'], $plist_info);
      $this->assertArrayNotHasKey($channel3_info['key'], $plist_info);
  }
}
