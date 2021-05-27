
class ClientChannel {
 public:
  ActivityPtr RunRPC(RpcContext* context,
                     std::function<void(absl::Status)> on_done) {
    return MakeActivity(
        [this]() {
          return Seq(
              Timeout(
                  context()->Deadline(),
                  TrySeq(
                      // First execute pre request extension points in order.
                      &pre_request_ops_,
                      // Next check for compression requirements... we create a
                      // new pipe and add a Join to execute compress/decompress
                      // alongside the main control path.
                      compression_filter_->RunRPC([this]() {
                        // Fetch the service config, and use it to add
                        // configuration and control to this request.
                        return config_.Watch(
                            [](ConfigPtr config, WatchCommitter committer) {
                              config->RunRPC(std::move(committer));
                            });
                      }))),
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
        // Keep running down the stack
        std::move(and_then),
        // Inject a loop to copy capsules (comrpessing along the way)
        // C++14 for brevity
        [compress = std::move(compress)]() {
          auto compressor =
              CompressorFromMetadata(context()->initial_metadata());
          // For each outgoing message, compress that message.
          return ForEach(compress.receiver,
                         Seq(Visitor(
                                 [](Slice slice) {
                                   // Note this may block! So we could dispatch
                                   // to a thread pool say to do the
                                   // compression.
                                   return compressor->CompressSlice(&slice);
                                 },
                                 [](Metadata metadata) {
                                   return ready(Capsule(metadata));
                                 }),
                             [&compress](Capsule c) {
                               return compress.sender.Push(std::move(c));
                             }));
        },
        [decompress = std::move(decompress)]() {
          DecompressorPtr decompressor;
          return TrySeq(
              // First read returned initial metadata, to ascertain the format
              // we should be using.
              Seq(decompress.receiver.Next(),
                  Visitor(
                      [](Metadata metadata) {
                        return ready(DecompressorFromMetadata(&metadata));
                      },
                      [](auto x) { return ready(absl::CancelledError()); }),
                  // Now we can loop over the remaining capsules and decompress
                  // them as needed.
                  [&decompress](DecompressorPtr decompressor) {
                    return ForEach(
                        decompress.receiver,
                        Seq(Visitor(
                                [decompressor =
                                     std::move(decompressor)](Slice slice) {
                                  return decompressor->DecompressSlice(slice);
                                },
                                [](Metadata metadata) {
                                  return ready(Capsule(metadata));
                                }),
                            [&decompress](Capsule c) {
                              return decompress.sender.Push(std::move(c));
                            }));
                  }))
        });
  }
};

class Config {
 public:
  Promise<absl::Status> RunRPC(WatchCommitter committer) {
    // Apply the config's deadline.
    return Timeout(config.Deadline(), [this, committer]() {
      // Use the config to follow load balancing and
      // choose a subchannel.
      auto lb_picker = lb_picker_.MakeObserver();
      return Seq(While(Seq(lb_picker.Next(),
                           [](LBPickerPtr picker) { return picker->Pick(); })),
                 [committer](SubchannelPtr subchannel) {
                   // Stop watching service config, we're
                   // committing to this subchannel.
                   committer.Commit();
                   return subchannel->RunRPC();
                 });
    });
  }

 private:
  Observable<LBPicker> lb_picker_;
};
