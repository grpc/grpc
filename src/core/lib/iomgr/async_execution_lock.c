#include "src/core/lib/iomgr/async_execution_lock.h"

#define NO_CONSUMER ((gpr_atm)1)

void grpc_aelock_init(grpc_aelock *lock, grpc_workqueue *optional_workqueue) {
  lock->optional_workqueue = optional_workqueue;
  gpr_atm_no_barrier_store(&lock->head, NO_CONSUMER);
  lock->tail = &lock->stub;
  gpr_atm_no_barrier_store(&lock->next, 0);
}

void grpc_aelock_destroy(grpc_aelock *lock) {
  GPR_ASSERT(gpr_atm_no_barrier_load(&lock->head) == NO_CONSUMER);
}

static void finish(grpc_exec_ctx *exec_ctx, grpc_aelock *lock) {
  for (;;) {
    grpc_aelock_qnode *tail = lock->tail;
    grpc_aelock_qnode *next =
        (grpc_aelock_qnode *)gpr_atm_acq_load(&tail->next);
    if (next == NULL) {
      if (gpr_atm_rel_cas(&lock->head, (gpr_atm)&lock->stub, NO_CONSUMER)) {
        return;
      }
    } else {
      lock->tail = next;

      next->action(exec_ctx, next->arg);
      gpr_free(next);
    }
  }
}

void grpc_aelock_execute(grpc_exec_ctx *exec_ctx, grpc_aelock *lock,
                         grpc_aelock_action action, void *arg,
                         size_t sizeof_arg) {
retry_top:
  gpr_atm cur = gpr_atm_acq_load(&lock->head);
  if (cur == NO_CONSUMER) {
    if (!gpr_atm_rel_cas(&lock->head, NO_CONSUMER, (gpr_atm)&lock->stub)) {
      goto retry_top;
    }
    action(exec_ctx, arg);
    finish(exec_ctx, lock);
    return;  // early out
  }

  grpc_aelock_qnode *n = gpr_malloc(sizeof(*n) + sizeof_arg);
  n->action = action;
  gpr_atm_no_barrier_store(&n->next, 0);
  if (sizeof_arg > 0) {
    memcpy(n + 1, arg, sizeof_arg);
    n->arg = n + 1;
  } else {
    n->arg = arg;
  }
  while (!gpr_atm_rel_cas(&lock->head, cur, (gpr_atm)n)) {
  retry_queue_load:
    cur = gpr_atm_acq_load(&lock->head);
    if (cur == NO_CONSUMER) {
      if (!gpr_atm_rel_cas(&lock->head, NO_CONSUMER, (gpr_atm)&lock->stub)) {
        goto retry_queue_load;
      }
      gpr_free(n);
      action(exec_ctx, arg);
      finish(exec_ctx, lock);
      return;  // early out
    }
  }
  gpr_atm_no_barrier_store(&((grpc_aelock_qnode *)cur)->next, (gpr_atm)n);
}
