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

#ifndef GRPC_TEST_CORE_END2END_CQ_VERIFIER_INTERNAL_H
#define GRPC_TEST_CORE_END2END_CQ_VERIFIER_INTERNAL_H

#include "test/core/end2end/cq_verifier.h"

typedef struct expectation expectation;

expectation* cq_verifier_get_first_expectation(cq_verifier* v);

void cq_verifier_set_first_expectation(cq_verifier* v, expectation* e);

grpc_event cq_verifier_next_event(cq_verifier* v, int timeout_seconds);

#endif /* GRPC_TEST_CORE_END2END_CQ_VERIFIER_INTERNAL_H */
