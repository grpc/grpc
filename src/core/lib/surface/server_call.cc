grpc_call_error ServerCall::StartBatch(const grpc_op* ops, size_t nops,
                                       void* notify_tag,
                                       bool is_notify_tag_closure) {
  if (nops == 0) {
    EndOpImmediately(cq_, notify_tag, is_notify_tag_closure);
    return GRPC_CALL_OK;
  }
  const grpc_call_error validation_result = ValidateServerBatch(ops, nops);
  if (validation_result != GRPC_CALL_OK) {
    return validation_result;
  }
  CommitBatch(ops, nops, notify_tag, is_notify_tag_closure);
  return GRPC_CALL_OK;
}

void ServerCall::CommitBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                             bool is_notify_tag_closure) {
  BatchOpIndex op_index(ops, nops);
  if (!is_notify_tag_closure) grpc_cq_begin_op(cq_, notify_tag);
  auto send_initial_metadata =
      op_index.OpHandler<GRPC_OP_SEND_INITIAL_METADATA>([this](
                                                            const grpc_op& op) {
        auto metadata = arena()->MakePooled<ServerMetadata>();
        PrepareOutgoingInitialMetadata(op, *metadata);
        CToMetadata(op.data.send_initial_metadata.metadata,
                    op.data.send_initial_metadata.count, metadata.get());
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%s[call] Send initial metadata",
                  DebugTag().c_str());
        }
        return [this, metadata = std::move(metadata)]() mutable {
          return call_handler_.PushServerInitialMetadata(std::move(metadata));
        };
      });
  auto send_message =
      op_index.OpHandler<GRPC_OP_SEND_MESSAGE>([this](const grpc_op& op) {
        SliceBuffer send;
        grpc_slice_buffer_swap(
            &op.data.send_message.send_message->data.raw.slice_buffer,
            send.c_slice_buffer());
        auto msg = arena()->MakePooled<Message>(std::move(send), op.flags);
        return [this, msg = std::move(msg)]() mutable {
          return call_handler_.PushMessage(std::move(msg));
        };
      });
  auto send_trailing_metadata =
      op_index.OpHandler<GRPC_OP_SEND_STATUS_FROM_SERVER>(
          [this](const grpc_op& op) {
            auto metadata = arena()->MakePooled<ServerMetadata>();
            CToMetadata(op.data.send_status_from_server.trailing_metadata,
                        op.data.send_status_from_server.trailing_metadata_count,
                        metadata.get());
            metadata->Set(GrpcStatusMetadata(),
                          op.data.send_status_from_server.status);
            if (auto* details =
                    op.data.send_status_from_server.status_details) {
              // TODO(ctiller): this should not be a copy, but we have
              // callers that allocate and pass in a slice created with
              // grpc_slice_from_static_string and then delete the string
              // after passing it in, which shouldn't be a supported API.
              metadata->Set(GrpcMessageMetadata(),
                            Slice(grpc_slice_copy(*details)));
            }
            CHECK(metadata != nullptr);
            return [this, metadata = std::move(metadata)]() mutable {
              CHECK(metadata != nullptr);
              return [this, metadata = std::move(
                                metadata)]() mutable -> Poll<Success> {
                CHECK(metadata != nullptr);
                call_handler_.PushServerTrailingMetadata(std::move(metadata));
                return Success{};
              };
            };
          });
  auto recv_message =
      op_index.OpHandler<GRPC_OP_RECV_MESSAGE>([this](const grpc_op& op) {
        return message_receiver_.MakeBatchOp(op, &call_handler_);
      });
  auto primary_ops = AllOk<StatusFlag>(
      TrySeq(AllOk<StatusFlag>(std::move(send_initial_metadata),
                               std::move(send_message)),
             std::move(send_trailing_metadata)),
      std::move(recv_message));
  if (auto* op = op_index.op(GRPC_OP_RECV_CLOSE_ON_SERVER)) {
    auto recv_trailing_metadata = OpHandler<GRPC_OP_RECV_CLOSE_ON_SERVER>(
        [this, cancelled = op->data.recv_close_on_server.cancelled]() {
          return Map(call_handler_.WasCancelled(),
                     [cancelled, this](bool result) -> Success {
                       ResetDeadline();
                       *cancelled = result ? 1 : 0;
                       return Success{};
                     });
        });
    call_handler_.SpawnInfallible(
        "final-batch", InfallibleBatch(std::move(primary_ops),
                                       std::move(recv_trailing_metadata),
                                       is_notify_tag_closure, notify_tag, cq_));
  } else {
    call_handler_.SpawnInfallible(
        "batch", FallibleBatch(std::move(primary_ops), is_notify_tag_closure,
                               notify_tag, cq_));
  }
}

grpc_call* MakeServerCall(CallHandler call_handler,
                          ClientMetadataHandle client_initial_metadata,
                          ServerInterface* server, grpc_completion_queue* cq,
                          grpc_metadata_array* publish_initial_metadata) {
  PublishMetadataArray(client_initial_metadata.get(), publish_initial_metadata,
                       false);
  // TODO(ctiller): ideally we'd put this in the arena with the CallHandler,
  // but there's an ownership problem: CallHandler owns the arena, and so would
  // get destroyed before the base class Call destructor runs, leading to
  // UB/crash. Investigate another path.
  return (new ServerCall(std::move(client_initial_metadata),
                         std::move(call_handler), server, cq))
      ->c_ptr();
}
