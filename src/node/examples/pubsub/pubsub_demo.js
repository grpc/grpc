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

'use strict';

var async = require('async');
var fs = require('fs');
var GoogleAuth = require('google-auth-library');
var parseArgs = require('minimist');
var strftime = require('strftime');
var _ = require('underscore');
var grpc = require('../..');
var PROTO_PATH = __dirname + '/pubsub.proto';
var pubsub = grpc.load(PROTO_PATH).tech.pubsub;

function PubsubRunner(pub, sub, args) {
  this.pub = pub;
  this.sub = sub;
  this.args = args;
}

PubsubRunner.prototype.getTestTopicName = function() {
  var base_name = '/topics/' + this.args.project_id + '/';
  if (this.args.topic_name) {
    return base_name + this.args.topic_name;
  }
  var now_text = strftime('%Y%m%d%H%M%S%L');
  return base_name + process.env.USER + '-' + now_text;
};

PubsubRunner.prototype.getTestSubName = function() {
  var base_name = '/subscriptions/' + this.args.project_id + '/';
  if (this.args.sub_name) {
    return base_name + this.args.sub_name;
  }
  var now_text = strftime('%Y%m%d%H%M%S%L');
  return base_name + process.env.USER + '-' + now_text;
};

PubsubRunner.prototype.listProjectTopics = function(callback) {
  var q = ('cloud.googleapis.com/project in (/projects/' +
      this.args.project_id + ')');
  this.pub.listTopics({query: q}, callback);
};

PubsubRunner.prototype.topicExists = function(name, callback) {
  this.listProjectTopics(function(err, response) {
    if (err) {
      callback(err);
    } else {
      callback(null, _.some(response.topic, function(t) {
        return t.name === name;
      }));
    }
  });
};

PubsubRunner.prototype.createTopicIfNeeded = function(name, callback) {
  var self = this;
  this.topicExists(name, function(err, exists) {
    if (err) {
      callback(err);
    } else{
      if (exists) {
        callback(null);
      } else {
        self.pub.createTopic({name: name}, callback);
      }
    }
  });
};

PubsubRunner.prototype.removeTopic = function(callback) {
  var name = this.getTestTopicName();
  console.log('... removing Topic', name);
  this.pub.deleteTopic({topic: name}, function(err, value) {
    if (err) {
      console.log('Could not delete a topic: rpc failed with', err);
      callback(err);
    } else {
      console.log('removed Topic', name, 'OK');
      callback(null);
    }
  });
};

PubsubRunner.prototype.createTopic = function(callback) {
  var name = this.getTestTopicName();
  console.log('... creating Topic', name);
  this.pub.createTopic({name: name}, function(err, value) {
    if (err) {
      console.log('Could not create a topic: rpc failed with', err);
      callback(err);
    } else {
      console.log('created Topic', name, 'OK');
      callback(null);
    }
  });
};

PubsubRunner.prototype.listSomeTopics = function(callback) {
  console.log('Listing topics');
  console.log('-------------_');
  this.listProjectTopics(function(err, response) {
    if (err) {
      console.log('Could not list topic: rpc failed with', err);
      callback(err);
    } else {
      _.each(response.topic, function(t) {
        console.log(t.name);
      });
      callback(null);
    }
  });
};

PubsubRunner.prototype.checkExists = function(callback) {
  var name = this.getTestTopicName();
  console.log('... checking for topic', name);
  this.topicExists(name, function(err, exists) {
    if (err) {
      console.log('Could not check for a topics: rpc failed with', err);
      callback(err);
    } else {
      if (exists) {
        console.log(name, 'is a topic');
      } else {
        console.log(name, 'is not a topic');
      }
      callback(null);
    }
  });
};

PubsubRunner.prototype.randomPubSub = function(callback) {
  var self = this;
  var topic_name = this.getTestTopicName();
  var sub_name = this.getTestSubName();
  var subscription = {name: sub_name, topic: topic_name};
  async.waterfall([
    _.bind(this.createTopicIfNeeded, this, topic_name),
    _.bind(this.sub.createSubscription, this.sub, subscription),
    function(resp, cb) {
      var msg_count = _.random(10, 30);
      // Set up msg_count messages to publish
      var message_senders = _.times(msg_count, function(n) {
        return _.bind(self.pub.publish, self.pub, {
          topic: topic_name,
          message: {data: new Buffer('message ' + n)}
        });
      });
      async.parallel(message_senders, function(err, result) {
        cb(err, result, msg_count);
      });
    },
    function(result, msg_count, cb) {
      console.log('Sent', msg_count, 'messages to', topic_name + ',',
                  'checking for them now.');
      var batch_request = {
        subscription: sub_name,
        max_events: msg_count
      };
      self.sub.pullBatch(batch_request, cb);
    },
    function(batch, cb) {
      var ack_id = _.pluck(batch.pull_responses, 'ack_id');
      console.log('Got', ack_id.length, 'messages, acknowledging them...');
      var ack_request = {
        subscription: sub_name,
        ack_id: ack_id
      };
      self.sub.acknowledge(ack_request, cb);
    },
    function(result, cb) {
      console.log(
          'Test messages were acknowledged OK, deleting the subscription');
      self.sub.deleteSubscription({subscription: sub_name}, cb);
    }
  ], function (err, result) {
    if (err) {
      console.log('Could not do random pub sub: rpc failed with', err);
    }
    callback(err, result);
  });
};

function main(callback) {
  var argv = parseArgs(process.argv, {
    string: [
      'host',
      'oauth_scope',
      'port',
      'action',
      'project_id',
      'topic_name',
      'sub_name'
    ],
    default: {
      host: 'pubsub-staging.googleapis.com',
      oauth_scope: 'https://www.googleapis.com/auth/pubsub',
      port: 443,
      action: 'listSomeTopics',
      project_id: 'stoked-keyword-656'
    }
  });
  var valid_actions = [
    'createTopic',
    'removeTopic',
    'listSomeTopics',
    'checkExists',
    'randomPubSub'
  ];
  if (_.some(valid_actions, function(action) {
    return action === argv.action;
  })) {
    callback(new Error('Action was not valid'));
  }
  var address = argv.host + ':' + argv.port;
  (new GoogleAuth()).getApplicationDefault(function(err, credential) {
    if (err) {
      callback(err);
      return;
    }
    if (credential.createScopedRequired()) {
      credential = credential.createScoped(argv.oauth_scope);
    }
    var updateMetadata = grpc.getGoogleAuthDelegate(credential);
    var ca_path = process.env.SSL_CERT_FILE;
    fs.readFile(ca_path, function(err, ca_data) {
      if (err) {
        callback(err);
        return;
      }
      var ssl_creds = grpc.Credentials.createSsl(ca_data);
      var options = {
        credentials: ssl_creds,
        'grpc.ssl_target_name_override': argv.host
      };
      var pub = new pubsub.PublisherService(address, options, updateMetadata);
      var sub = new pubsub.SubscriberService(address, options, updateMetadata);
      var runner = new PubsubRunner(pub, sub, argv);
      runner[argv.action](callback);
    });
  });
}

if (require.main === module) {
  main(function(err) {
    if (err) {
      throw err;
    }
  });
}

module.exports = PubsubRunner;
