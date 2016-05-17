#!/usr/bin/env ruby

# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# pubsub_demo demos accesses the Google PubSub API via its gRPC interface
#
# $ GOOGLE_APPLICATION_CREDENTIALS=<path_to_service_account_key_file> \
#   path/to/pubsub_demo.rb \
#   [--action=<chosen_demo_action> ]
#
# There are options related to the chosen action, see #parse_args below.
# - the possible actions are given by the method names of NamedAction class
# - the default action is list_some_topics

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(File.dirname(this_dir)), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'optparse'

require 'grpc'
require 'googleauth'
require 'google/protobuf'

require 'google/protobuf/empty'
require 'tech/pubsub/proto/pubsub'
require 'tech/pubsub/proto/pubsub_services'

# creates a SSL Credentials from the production certificates.
def ssl_creds
  GRPC::Core::ChannelCredentials.new()
end

# Builds the metadata authentication update proc.
def auth_proc(opts)
  auth_creds = Google::Auth.get_application_default
  return auth_creds.updater_proc
end

# Creates a stub for accessing the publisher service.
def publisher_stub(opts)
  address = "#{opts.host}:#{opts.port}"
  stub_clz = Tech::Pubsub::PublisherService::Stub # shorter
  GRPC.logger.info("... access PublisherService at #{address}")
  call_creds = GRPC::Core::CallCredentials.new(auth_proc(opts))
  combined_creds = ssl_creds.compose(call_creds)
  stub_clz.new(address, creds: combined_creds,
               GRPC::Core::Channel::SSL_TARGET => opts.host)
end

# Creates a stub for accessing the subscriber service.
def subscriber_stub(opts)
  address = "#{opts.host}:#{opts.port}"
  stub_clz = Tech::Pubsub::SubscriberService::Stub # shorter
  GRPC.logger.info("... access SubscriberService at #{address}")
  call_creds = GRPC::Core::CallCredentials.new(auth_proc(opts))
  combined_creds = ssl_creds.compose(call_creds)
  stub_clz.new(address, creds: combined_creds,
               GRPC::Core::Channel::SSL_TARGET => opts.host)
end

# defines methods corresponding to each interop test case.
class NamedActions
  include Tech::Pubsub

  # Initializes NamedActions
  #
  # @param pub [Stub] a stub for accessing the publisher service
  # @param sub [Stub] a stub for accessing the publisher service
  # @param args [Args] provides access to the command line
  def initialize(pub, sub, args)
    @pub = pub
    @sub = sub
    @args = args
  end

  # Removes the test topic if it exists
  def remove_topic
    name = test_topic_name
    p "... removing Topic #{name}"
    @pub.delete_topic(DeleteTopicRequest.new(topic: name))
    p "removed Topic: #{name} OK"
  rescue GRPC::BadStatus => e
    p "Could not delete a topics: rpc failed with '#{e}'"
  end

  # Creates a test topic
  def create_topic
    name = test_topic_name
    p "... creating Topic #{name}"
    resp = @pub.create_topic(Topic.new(name: name))
    p "created Topic: #{resp.name} OK"
  rescue GRPC::BadStatus => e
    p "Could not create a topics: rpc failed with '#{e}'"
  end

  # Lists topics in the project
  def list_some_topics
    p 'Listing topics'
    p '-------------_'
    list_project_topics.topic.each { |t| p t.name }
  rescue GRPC::BadStatus => e
    p "Could not list topics: rpc failed with '#{e}'"
  end

  # Checks if a topics exists in a project
  def check_exists
    name = test_topic_name
    p "... checking for topic #{name}"
    exists = topic_exists?(name)
    p "#{name} is a topic" if exists
    p "#{name} is not a topic" unless exists
  rescue GRPC::BadStatus => e
    p "Could not check for a topics: rpc failed with '#{e}'"
  end

  # Publishes some messages
  def random_pub_sub
    topic_name, sub_name = test_topic_name, test_sub_name
    create_topic_if_needed(topic_name)
    @sub.create_subscription(Subscription.new(name: sub_name,
                                              topic: topic_name))
    msg_count = rand(10..30)
    msg_count.times do |x|
      msg = PubsubMessage.new(data: "message #{x}")
      @pub.publish(PublishRequest.new(topic: topic_name, message: msg))
    end
    p "Sent #{msg_count} messages to #{topic_name}, checking for them now."
    batch = @sub.pull_batch(PullBatchRequest.new(subscription: sub_name,
                                                 max_events: msg_count))
    ack_ids = batch.pull_responses.map { |x| x.ack_id }
    p "Got #{ack_ids.size} messages; acknowledging them.."
    @sub.acknowledge(AcknowledgeRequest.new(subscription: sub_name,
                                            ack_id: ack_ids))
    p "Test messages were acknowledged OK, deleting the subscription"
    del_req = DeleteSubscriptionRequest.new(subscription: sub_name)
    @sub.delete_subscription(del_req)
  rescue GRPC::BadStatus => e
    p "Could not do random pub sub: rpc failed with '#{e}'"
  end

  private

  # test_topic_name is the topic name to use in this test.
  def test_topic_name
    unless @args.topic_name.nil?
      return "/topics/#{@args.project_id}/#{@args.topic_name}"
    end
    now_text = Time.now.utc.strftime('%Y%m%d%H%M%S%L')
    "/topics/#{@args.project_id}/#{ENV['USER']}-#{now_text}"
  end

  # test_sub_name is the subscription name to use in this test.
  def test_sub_name
    unless @args.sub_name.nil?
      return "/subscriptions/#{@args.project_id}/#{@args.sub_name}"
    end
    now_text = Time.now.utc.strftime('%Y%m%d%H%M%S%L')
    "/subscriptions/#{@args.project_id}/#{ENV['USER']}-#{now_text}"
  end

  # determines if the topic name exists
  def topic_exists?(name)
    topics = list_project_topics.topic.map { |t| t.name }
    topics.include?(name)
  end

  def create_topic_if_needed(name)
    return if topic_exists?(name)
    @pub.create_topic(Topic.new(name: name))
  end

  def list_project_topics
    q = "cloud.googleapis.com/project in (/projects/#{@args.project_id})"
    @pub.list_topics(ListTopicsRequest.new(query: q))
  end
end

# Args is used to hold the command line info.
Args = Struct.new(:host, :port, :action, :project_id, :topic_name,
                  :sub_name)

# validates the the command line options, returning them as an Arg.
def parse_args
  args = Args.new('pubsub-staging.googleapis.com',
                   443, 'list_some_topics', 'stoked-keyword-656')
  OptionParser.new do |opts|
    opts.on('--server_host SERVER_HOST', 'server hostname') do |v|
      args.host = v
    end
    opts.on('--server_port SERVER_PORT', 'server port') do |v|
      args.port = v
    end

    # instance_methods(false) gives only the methods defined in that class.
    scenes = NamedActions.instance_methods(false).map { |t| t.to_s }
    scene_list = scenes.join(',')
    opts.on("--action CODE", scenes, {}, 'pick a demo action',
            "  (#{scene_list})") do |v|
      args.action = v
    end

    # Set the remaining values.
    %w(project_id topic_name sub_name).each do |o|
      opts.on("--#{o} VALUE", "#{o}") do |v|
        args[o] = v
      end
    end
  end.parse!
  _check_args(args)
end

def _check_args(args)
  %w(host port action).each do |a|
    if args[a].nil?
      raise OptionParser::MissingArgument.new("please specify --#{a}")
    end
  end
  args
end

def main
  args = parse_args
  pub, sub = publisher_stub(args), subscriber_stub(args)
  NamedActions.new(pub, sub, args).method(args.action).call
end

main
