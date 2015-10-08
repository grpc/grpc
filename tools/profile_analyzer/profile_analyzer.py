#!/usr/bin/env python2.7
import json
import collections
import itertools


SELF_TIME = object()
TIME_FROM_SCOPE_START = object()
TIME_TO_SCOPE_END = object()
TIME_FROM_STACK_START = object()
TIME_TO_STACK_END = object()


class LineItem(object):

	def __init__(self, line, indent):
		self.tag = line['tag']
		self.indent = indent
		self.time_stamp = line['t']
		self.important = line['type'] == '!'
		self.times = {}


class ScopeBuilder(object):

	def __init__(self, call_stack_builder, line):
		self.call_stack_builder = call_stack_builder
		self.indent = len(call_stack_builder.stk)
		self.top_line = LineItem(line, self.indent)
		call_stack_builder.lines.append(self.top_line)
		self.first_child_pos = len(call_stack_builder.lines)

	def mark(self, line):
		pass

	def finish(self, line):
		assert line['tag'] == self.top_line.tag
		final_time_stamp = line['t']
		assert SELF_TIME not in self.top_line.times
		self.top_line.tims[SELF_TIME] = final_time_stamp - self.top_line.time_stamp
		for line in self.call_stack_builder.lines[self.first_child_pos:]:
			if TIME_FROM_SCOPE_START not in line.times:
				line[TIME_FROM_SCOPE_START] = line.time_stamp - self.top_line.time_stamp
				line[TIME_TO_SCOPE_END] = final_time_stamp - line.time_stamp


class CallStackBuilder(object):
	
	def __init__(self):
		self.stk = []
		self.signature = ''
		self.lines = []

	def add(self, line):
		line_type = line['type']
		self.signature = '%s%s%s' % (self.signature, line_type, line['tag'])
		if line_type == '{':
			self.stk.append(ScopeBuilder(self, line))
			return False
		elif line_type == '}':
			self.stk.pop().finish(line)
			return not self.stk
		elif line_type == '.' or line_type == '!':
			self.stk[-1].mark(line, True)
			return False
		else:
			raise Exception('Unknown line type: \'%s\'' % line_type)


class CallStack(object):

	def __init__(self, initial_call_stack_builder):
		self.count = 1
		self.signature = initial_call_stack_builder.signature
		self.lines = initial_call_stack_builder.lines
		for line in lines:
			for key, val in line.times.items():
				line.times[key] = [val]

	def add(self, call_stack_builder):
		assert self.signature == call_stack_builder.signature
		self.count += 1
		assert len(self.lines) == len(call_stack_builder.lines)
		for lsum, line in itertools.izip(self.lines, call_stack_builder.lines):
			assert lsum.tag == line.tag
			assert lsum.times.keys() == line.times.keys()
			for k, lst in lsum.times.iterkeys():
				lst.append(line.times[k])


builder = collections.defaultdict(CallStackBuilder)
call_stacks = collections.defaultdict(CallStack)

with open('latency_trace.txt') as f:
  for line in f:
    inf = json.loads(line)
    thd = inf['thd']
    cs = builder[thd]
    if cs.add(inf):
    	if cs.signature in call_stacks:
    		call_stacks[cs.signature].add(cs)
    	else:
    		call_stacks[cs.signature] = CallStack(cs)
    	del builder[thd]

call_stacks = sorted(call_stacks.values(), key=lambda cs: cs.count, reverse=True)

for cs in call_stacks:
	print cs.signature
	print cs.count
