// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

class ClientChannel {
 public:
  ActivityPtr RunRPC(RpcContext* context,
                     std::function<void(absl::Status)> on_done) {
    return MakeActivity(
        [this]() {
          return Seq(
              Race(Timeout(context()->Deadline()),
                   TrySeq(
                       // First execute pre request extension points in order.
                       &pre_request_ops_,
                       TryJoin(
                           // Kick off the compression filter - it's going to
                           // keep a few things going concurrently to mutate
                           // messages as they come through.
                           compression_filter_->RunRPC(),
                           // And concurrently, continue the RPC by retrieving a
                           // service config and using it.
                           Seq(config_.Next(),
                               [](ConfigPtr config) {
                                 return config->RunRPC();
                               })))),
              // Finally execute any post request extension points in order.
              &post_request_ops_);
        },
        std::move(on_done), callback_scheduler_, context);
  }

 private:
  CallbackScheduler* const callback_scheduler_;
  ActivityPtr control_plane_;
  CompressionFilterPtr compression_filter_;
  Observable<ConfigPtr> config_;
  // EXTENSIBILITY POINT: pre request ops fire before the request is sent, and
  // can perform arbitrary computation/blocking. Once they complete, they
  // complete finally.
  std::vector<std::function<Promise<absl::Status>()>> pre_request_ops_;
  // EXTENSIBILITY POINT: post request ops fire after the request is complete,
  // and can perform arbitrary computation/blocking. Once they complete, they
  // complete finally. They act as a mapping from the proposed status to the
  // final status.
  std::vector<std::function<Promise<absl::Status>(absl::Status)>>
      post_request_ops_;
};

class CompressionFilter {
 public:
  Promise<absl::Status> RunRPC(Promise<absl::Status> and_then) {
    Pipe compress;
    Pipe decompress;
    // Substitute our pipes for the context pipes.
    context()->SwapSendReceiver(&compress.receiver);
    context()->SwapRecvSender(&decompress.sender);
    return TryJoin(
        // Inject a loop to copy capsules (comrpessing along the way)
        // C++14 for brevity
        [compress = std::move(compress)]() {
          GetContext<RPC>()->set_compressor(
              CompressorFromMetadata(context()->initial_metadata()));
          // For each outgoing message, compress that message.
          return compress.receiver.Filter([compressor](Capsule capsule) {
            return Visitor(
                [](Slice slice) {
                  // Note this may block! So we could dispatch
                  // to a thread pool say to do the
                  // compression.
                  return GetContext<RPC>()->compressor()->CompressSlice(&slice);
                },
                [](Metadata metadata) { return ready(Capsule(metadata)); });
          });
        },
        [decompress = std::move(decompress)]() {
          DecompressorPtr decompressor;
          return decompress.sender.Filter([decompressor](Capsule capsule) {
            return Visitor(
                [](Metadata metadata) {
                  GetContext<RPC>()->set_decompressor(
                      DecompressorFromMetadata(&metadata));
                  return ready(std::move(metadata));
                },
                [](Slice slice) {
                  return GetContext<RPC>()->decompressor()->DecompressSlice(
                      &slice);
                })
          });
        });
  }
};

class Config {
 public:
  Promise<absl::Status> RunRPC() {
    // Apply the config's deadline.
    return Race(Timeout(config.Deadline()), [this]() {
      // Use the config to follow load balancing and
      // choose a subchannel.
      auto lb_picker = lb_picker_.MakeObserver();
      return Seq(While(Seq(lb_picker.Next(),
                           [](LBPickerPtr picker) { return picker->Pick(); })),
                 [](SubchannelPtr subchannel) { return subchannel->RunRPC(); });
    });
  }

 private:
  Observable<LBPicker> lb_picker_;
};

class Filter {
 public:
  virtual Promise<CallStatus> CreatePromise(Filter* next) = 0;
};

class BufferedPipe {
 public:
  // Take all the messages from the main request pipe, and buffer them into our
  // internal state.
  Promise<absl::Status> BufferFrom(PipeSender<Capsule> sender) {
    auto state = state_;
    return Seq(ForEach(std::move(sender),
                       [state](Capsule c) {
                         state->buffer.emplace_back(std::move(c));
                         send_waiters.Wake();
                       }),
               []() {
                 state->closed = true;
                 send_waiters.Wake();
                 return absl::OkStatus();
               });
  }

  // Return type for MakeSender
  struct Outgoing {
    // The pipe to push down the stack to receive capsules from the transport.
    PipeReceiver<Capsule> receive_pipe;
    // A promise to run concurrently with execution to deal with forwarding.
    Promise<absl::Status> proxy_promise;
  };

  // Make a single sender of the buffered output.
  // Multiple senders can exist at once, and each will send the same buffered
  // messages.
  Outgoing MakeSender() {
    Pipe<Capsule> pipe;
    auto state = state_;
    size_t i = 0;
    auto send_pipe = std::move(pipe.sender);
    auto get_next_capsule = [state,
                             i]() -> Poll<absl::Optional<Capsule>> mutable {
      // Are we at the end of the buffer?
      if (i == state->buffer.size()) {
        // If we are, is the stream also closed?
        if (state->closed) {
          return absl::nullopt;
        } else {
          // If not, record that we're waiting.
          return state->send_waiters.pending();
        }
      }
      // There's a capsule that we haven't passed down yet, begin to do
      // so.
      return state->buffer[i++].Clone();
    };
    auto maybe_forward_capsule =
        [GRPC_CAPTURE(send_pipe)](absl::optional<Capsule> capsule) {
          return If(
              capsule.has_value(),
              // If we have a capsule to send, then do so and return true if
              // it was sent.
              [GRPC_CAPTURE(capsule), GRPC_CAPTURE(send_pipe)]() {
                return send_pipe.Send(*capsule);
              },
              // Otherwise return false, which will terminate the loop.
              Immediately(false));
        };
    return Outgoing{std::move(pipe.receiver),
                    While(Seq(get_next_capsule, maybe_forward_capsule))};
  }

 private:
  struct State {
    absl::InlinedVector<Capsule, 2> buffer;
    bool closed = false;
    IntraActivityWaiter send_waiters;
  };
  std::shared_ptr<State> state_;
}

class RetryFilter final : public Filter {
 public:
  virtual Promise<CallStatus> CreatePromise(Filter* next,
                                            PromiseArgs promise_args) {
    BufferedPipe<Capsule> buffered_pipe;
    auto initial_metadata = std::move(promise_args.initial_metadata());
    auto outgoing_pipe = std::move(promise_args.outgoing_pipe());
    auto incoming_pipe = std::move(promise_args.incoming_pipe());
    struct RetryData {
      int attempts = 0;
      bool committed = false;
      grpc_millis server_pushback_ms = 0;
    };
    bool* retry_data = NewFromArena<RetryData>();
    auto retry_policy = retry_policy_;
    auto apply_pushback = [retry_data]() {
      // If the server has requested some pushback time, then sleep here.
      return Wait(retry_data->server_pushback_ms);
    };
    // Actual send attempt:
    auto make_one_attempt = [next, GRPC_CAPTURE(incoming_pipe),
                             GRPC_CAPTURE(initial_metadata),
                             GRPC_CAPTURE(buffered_pipe)]() {
      // Make a new pipe to interject on received messages.
      Pipe<Capsule> receive_pipe;
      // And another to send outgoing messages (that may or may not
      // already be buffered).
      auto outgoing = buffered_pipe.MakeSender();
      // New pipes and initial metadata to go down the stack.
      auto promise_args = PromiseArgs(initial_metadata.Clone(),
                                      std::move(outgoing.receive_pipe),
                                      std::move(receive_pipe.sender));
      return TryJoin(
          // BufferedPipe gives us a promise to run to forward outgoing
          // messages.
          std::move(outgoing.proxy_promise),
          // For each capsule received from the server, forward it up
          // the stack. We also become committed after receiving the
          // first capsule - meaning that no further retries will occur.
          ForEach(std::move(receive_pipe.receiver),
                  [retry_data, GRPC_CAPTURE(incoming_pipe)](Capsule c) {
                    retry_data->committed = true;
                    return incoming_pipe.Send(std::move(c))
                  }),
          // Ask the next filter along to create a promise that will
          // ultimately run the request and return some status.
          next->CreatePromise(std::move(promise_args)));
    };
    // Post running an attempt, we have some call status.
    // Interpret that and decide what to do.
    auto evaluate_result_and_maybe_continue =
        [retry_data, retry_policy](CallStatus status) {
          ++retry_data->attempts;
          // If the call succeeded or we became committed, or if the call is
          // not retryable (due to status code or too many attempts), then
          // just return the status we received up the stack.
          if (status.ok() || retry_data->committed ||
              !retry_policy->retryable_status_codes().Contains(status) ||
              retry_data->attempts >= retry_policy->max_attempts()) {
            return status;
          }
          // Capture the server pushback from trailing metadata.
          retry_data->server_pushback_ms =
              status.trailing_metadata().server_pushback_ms;
          // Continue with the next attempt.
          return kContinue;
        };
    return TryJoin(
        // Take messages being sent, and add them to our internal buffer so that
        // we can resend them at will.
        buffered_pipe.BufferFrom(std::move(outgoing_pipe)),
        // Main retry loop:
        Until(Seq(apply_pushback, make_one_attempt,
                  evaluate_result_and_maybe_continue)))
  }

 private:
  std::shared_ptr<RetryPolicy> retry_policy_;
};
