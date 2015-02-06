#!/usr/bin/python2.7

from copy import deepcopy

def create(state, name):
  me = getattr(state, name)
  if not me.created:
    new = me.copy()
    new.created = True
    cg = state.codegen.copy()
    cg.lines.extend([
        'config.init_%s(f, %s_args)' % (name, name)])
    s = State(state.client, state.server, cg)
    setattr(s, name, new)
    yield s

def start_client(state):
  if state.client.created and not state.client.started:
    cg = state.codegen.copy()
    cg.lines.extend([
        'client_call = grpc_channel_create_call(f.client, f.client_cq, "/foo", "test.google.com", deadline);'
        ])
    client = state.client.copy()
    client.started = True
    yield State(client, state.server, cg)

def request_server(state):
  if state.server.created and not state.server.requested:
    cg = state.codegen.copy()
    tag = cg.make_tag('request_server')
    cg.lines.extend([
        'GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, &server_call, &call_details, &request_metadata_recv, f.server_cq, tag(%d)))' % tag])
    server = state.server.copy()
    server.requested = True
    yield State(state.client, server, cg)

def start_op(state, l, r):
  local = getattr(state, l)
  remote = getattr(state, r)
  if not local.started:
    return
  for send_initial_metadata in [True, False]:
    for send_message in [True, False]:
      for send_close in [True, False]:
        for receive_initial_metadata in [True, False]:
          for receive_message in [True, False]:
            for receive_close in [True, False]:
              if ((not send_initial_metadata) and (not send_message) and (not send_close) and
                  (not receive_initial_metadata) and (not receive_message) and (not receive_close)):
                continue
              if local.sending_initial_metadata and send_initial_metadata: continue
              if local.sending_message and send_message: continue
              if local.sending_close and send_close: continue
              if l == 'server' and receive_initial_metadata: continue
              if local.receiving_initial_metadata and receive_initial_metadata: continue
              if local.receiving_message and receive_message: continue
              if local.receiving_close and receive_close: continue
              local2 = local.copy()
              cg = state.codegen.copy()
              cg.lines.extend(['op = ops']);
              tag = cg.make_tag('start_op_%s' % l)
              if send_initial_metadata:
                cg.lines.extend([
                    'op->type = GRPC_OP_SEND_INITIAL_METADATA;',
                    'op->data.send_initial_metadata.count = 0;',
                    'op++;'])
                local2.sending_initial_metadata = tag
              if send_message:
                cg.lines.extend([
                    'op->type = GRPC_OP_SEND_MESSAGE;',
                    'op->data.send_message = %s_payload;' % l,
                    'op++;'])
                local2.sending_message = tag
              if send_close:
                if l == 'client':
                  cg.lines.extend([
                      'op->type = GRPC_OP_SEND_CLOSE_FROM_CLIENT;',
                      'op++'])
                else:
                  cg.lines.extend([
                      'op->type = GRPC_OP_SEND_STATUS_FROM_SERVER;',
                      'op++'])
                local2.sending_close = tag
              if receive_initial_metadata:
                cg.lines.extend([
                    'op->type = GRPC_OP_RECV_INITIAL_METADATA;',
                    'op++'])
                local2.receiving_initial_metadata = tag
              if receive_message:
                cg.lines.extend([
                    'op->type = GRPC_OP_RECV_MESSAGE;',
                    'op++'])
                local2.receiving_message = tag
              if receive_close:
                if l == 'client':
                  cg.lines.extend([
                      'op->type = GRPC_OP_RECV_STATUS_ON_CLIENT;',
                      'op++'])
                else:
                  cg.lines.extend([
                      'op->type = GRPC_OP_RECV_CLOSE_ON_SERVER;',
                      'op++'])
                local2.receiving_close = tag
              cg.lines.extend([
                  'GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(%s_call, ops, op - ops, tag(%d)));' % (l, tag)])
              s = State(state.client, state.server, cg)
              setattr(s, l, local2)
              yield s

def wrap(f, *a):
  def g(state):
    for x in f(state, *a):
      yield x
  return g

MUTATORS = [
    wrap(create, 'client'),
    wrap(create, 'server'),
    wrap(start_op, 'client', 'server'),
    wrap(start_op, 'server', 'client'),
    start_client,
    request_server,
    ]

class Codegen(object):
  lines = []
  next_tag = 1
  last_tag_creator = 'nobody'
  def generate(self):
    print '{'
    print '\n'.join(self.lines)
    print '}'
  def copy(self):
    cg = deepcopy(self)
    cg.lines = self.lines[:]
    return cg
  def make_tag(self, name):
    self.last_tag_creator = name
    tag = self.next_tag
    self.next_tag += 1
    return tag

class Endpoint(object):
  created = False
  started = False
  requested = False
  sent_initial_metadata = False
  sent_messages = 0
  sent_close = False
  sending_initial_metadata = False
  sending_message = False
  sending_close = False
  received_initial_metadata = False
  received_messages = 0
  received_close = False
  receiving_initial_metadata = False
  receiving_message = False
  receiving_close = False

  def copy(self):
    return deepcopy(self)

class State(object):
  def __init__(self, client, server, codegen):
    self.client = client
    self.server = server
    self.codegen = codegen

  def as_dict(self):
    return {'client': self.client.as_dict(), 'server': self.server.as_dict()}

  def mutations(self):
    for mutator in MUTATORS:
      for new_state in mutator(self):
        yield new_state

count = 0
def generate(state, depth):
  global count
  n = 0
  #print ' '*depth, state.as_dict()
  for state2 in state.mutations():
    n += 1
    generate(state2, depth+1)
  if n == 0:
    count += 1
    #state.codegen.generate()

generate(State(Endpoint(), Endpoint(), Codegen()), 0)
print count

