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

#include <sys/time.h>

#include "test/core/util/test_config.h"
#include "src/core/profiling/timers.h"
#include "src/cpp/profiling/endoscope_service.h"
#include <grpc++/server_context.h>
#include <grpc++/status.h>
#include <gtest/gtest.h>
#include <grpc/grpc.h>

#ifdef GRPC_ENDOSCOPE_PROFILER

using perftools::endoscope::EndoRequestPB;
using perftools::endoscope::EndoSnapshotPB;
using perftools::endoscope::EndoAtomPB;

namespace grpc {
namespace testing {

class EndoscopeServiceTest : public ::testing::Test {
 protected:
  grpc::endoscope::EndoscopeService service_;
};

TEST_F(EndoscopeServiceTest, EndoscopeCoreTest) {
  grpc_endo_base *base = &grpc_endo_instance;
  GRPC_ENDO_INIT(base);
  GRPC_ENDO_BEGIN(base, "SOMEBODY SET UP US THE BOMB");
  GRPC_ENDO_BEGIN(base, "ALL YOUR BASE ARE BELONG TO US");
  GRPC_ENDO_EVENT(base, "YOU HAVE NO CHANCE TO SURVIVE MAKE YOUR TIME");
  GRPC_ENDO_END(base, NULL);
  GRPC_ENDO_END(base, NULL);

  // marker
  EXPECT_EQ("SOMEBODY SET UP US THE BOMB",
            string(base->marker_pool[0].name));
  EXPECT_EQ("ALL YOUR BASE ARE BELONG TO US",
            string(base->marker_pool[1].name));
  EXPECT_EQ("YOU HAVE NO CHANCE TO SURVIVE MAKE YOUR TIME",
            string(base->marker_pool[2].name));
  EXPECT_EQ(3, base->marker_count);

  // task
  EXPECT_EQ(0, base->task_pool[0].marker_id);
  EXPECT_EQ(0, base->task_pool[0].thread_index);
  EXPECT_EQ(0, base->task_pool[0].log_head);
  EXPECT_EQ(2, base->task_pool[0].log_tail);
  EXPECT_EQ(GRPC_ENDO_EMPTY, base->task_pool[0].next_task);
  EXPECT_EQ(GRPC_ENDO_EMPTY, base->task_pool[0].next_taskwithatom);
  EXPECT_EQ(0, base->task_pool[0].scope_depth);
  EXPECT_LT(base->task_pool[0].cycle_begin, base->task_pool[0].cycle_end);
  EXPECT_EQ(1, base->task_stack);
  EXPECT_EQ(0, base->task_history_head);
  EXPECT_EQ(0, base->task_history_tail);
  EXPECT_EQ(0, base->task_withatom_head);
  EXPECT_EQ(0, base->task_withatom_tail);
  EXPECT_EQ(1, base->task_count);

  // atom
  EXPECT_EQ(1, base->atom_pool[0].type);  // EndoAtomPB::SCOPE_BEGIN
  EXPECT_EQ(1, base->atom_pool[0].param);
  EXPECT_EQ(1, base->atom_pool[0].next_atom);
  EXPECT_EQ(5, base->atom_pool[1].type);  // EndoAtomPB::EVENT
  EXPECT_EQ(2, base->atom_pool[1].param);
  EXPECT_EQ(2, base->atom_pool[1].next_atom);
  EXPECT_EQ(2, base->atom_pool[2].type);  // EndoAtomPB::SCOPE_END
  EXPECT_EQ(GRPC_ENDO_EMPTY, base->atom_pool[2].next_atom);
  EXPECT_LT(base->atom_pool[0].cycle, base->atom_pool[1].cycle);
  EXPECT_LT(base->atom_pool[1].cycle, base->atom_pool[2].cycle);
  EXPECT_EQ(3, base->atom_stack);

  // thread
  EXPECT_EQ(GRPC_ENDO_EMPTY, base->thread_pool[0].task_active);
  EXPECT_EQ(1, base->thread_count);
}

TEST_F(EndoscopeServiceTest, ProtobufOutputTest) {
  grpc_endo_base *base = &grpc_endo_instance;
  GRPC_ENDO_INIT(base);
  GRPC_ENDO_BEGIN(base, "SOMEBODY SET UP US THE BOMB");
  GRPC_ENDO_BEGIN(base, "ALL YOUR BASE ARE BELONG TO US");
  GRPC_ENDO_EVENT(base, "YOU HAVE NO CHANCE TO SURVIVE MAKE YOUR TIME");
  GRPC_ENDO_END(base, NULL);
  GRPC_ENDO_END(base, NULL);

  EndoRequestPB request;
  EndoSnapshotPB snapshot;
  Status s = service_.Action(nullptr, &request, &snapshot);

  // marker
  EXPECT_EQ("SOMEBODY SET UP US THE BOMB",
            snapshot.marker(0).name());
  EXPECT_EQ("ALL YOUR BASE ARE BELONG TO US",
            snapshot.marker(1).name());
  EXPECT_EQ("YOU HAVE NO CHANCE TO SURVIVE MAKE YOUR TIME",
            snapshot.marker(2).name());
  EXPECT_EQ(3, snapshot.marker_size());

  // task
  EXPECT_EQ(0, snapshot.tasks_history(0).marker_id());
  EXPECT_LT(snapshot.tasks_history(0).cycle_begin(), snapshot.tasks_history(0).cycle_end());
  EXPECT_EQ(1, snapshot.tasks_history_size());
  EXPECT_EQ(0, snapshot.tasks_active_size());

  // atom
  EXPECT_EQ(EndoAtomPB::SCOPE_BEGIN, snapshot.tasks_history(0).log(0).type());
  EXPECT_EQ(1, snapshot.tasks_history(0).log(0).param());
  EXPECT_EQ(EndoAtomPB::EVENT, snapshot.tasks_history(0).log(1).type());
  EXPECT_EQ(2, snapshot.tasks_history(0).log(1).param());
  EXPECT_EQ(EndoAtomPB::SCOPE_END, snapshot.tasks_history(0).log(2).type());

  // thread
  EXPECT_EQ(snapshot.thread(0).thread_id(), snapshot.tasks_history(0).thread_id());
  EXPECT_EQ(1, snapshot.thread_size());
}

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_ENDOSCOPE_PROFILER

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
