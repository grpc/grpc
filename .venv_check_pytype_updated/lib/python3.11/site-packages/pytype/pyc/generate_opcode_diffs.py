"""Generate code diffs for opcode changes between Python versions.

Automates some of the tedious work of updating pytype to support a new Python
version. This script should be run from the command line as:

  python generate_opcode_diffs.py {old_version} {new_version}

For example, to generate diffs for updating from Python 3.8 to 3.9, use "3.8"
for {old_version} and "3.9" for {new_version}.

Requirements:
* Python 3.7+ to run this script
* Python interpreters for the versions you want to diff

The output has three sections:
* "NEW OPCODES" are new opcode classes that should be added to pyc/opcodes.py.
* "OPCODE MAPPING DIFF" is the content of a dictionary of opcode changes. Just
  copy the python_{major}_{minor}_mapping definition in pyc/opcodes.py for the
  previous version, change the version numbers, and replace the diff - it'll be
  obvious where it goes. Then add the new mapping to the mapping dict in the
  top-level dis function in the same module.
* "OPCODE STUB IMPLEMENTATIONS" are new methods that should be added to vm.py.
If the output contains "NOTE:" lines about modified opcodes, then there are
opcode names that exist in both versions but at different indices. For such
opcodes, the script generates a new class definition for pyc/opcodes.py but does
not generate a new stub implementation for vm.py.
"""

import json
import opcode
import subprocess
import sys
import tempfile
import textwrap


# Starting with Python 3.12, `dis` collections contain pseudo instructions and
# instrumented instructions. These are opcodes with values >= MIN_PSEUDO_OPCODE
# and >= MIN_INSTRUMENTED_OPCODE.
# Pytype doesn't care about those, so we ignore them here.
_MIN_INSTRUMENTED_OPCODE = getattr(opcode, 'MIN_INSTRUMENTED_OPCODE', 237)


def generate_diffs(argv):
  """Generate diffs."""
  version1, version2 = argv

  # Create a temporary script to print out information about the opcode mapping
  # of the Python version that the script is running under, and use subprocess
  # to run the script under the two versions we want to diff.
  # opmap is a mapping from opcode name to index, e.g. {'RERAISE': 119}.
  # opname is a sequence of opcode names in which the sequence index corresponds
  # to the opcode index. A name of "<i>" means that i is an unused index.
  with tempfile.NamedTemporaryFile(mode='w', suffix='.py') as f:
    f.write(textwrap.dedent("""
      import dis
      import json
      import opcode
      output = {
        'opmap': dis.opmap,
        'opname': dis.opname,
        'intrinsic_1_descs': getattr(opcode, '_intrinsic_1_descs', []),
        'intrinsic_2_descs': getattr(opcode, '_intrinsic_2_descs', []),
        'inline_cache_entries': getattr(opcode, '_inline_cache_entries', []),
        'HAVE_ARGUMENT': dis.HAVE_ARGUMENT,
        'HAS_CONST': dis.hasconst,
        'HAS_NAME': dis.hasname,
        'HAS_JREL': dis.hasjrel,
        'HAS_JABS': dis.hasjabs,
        'HAS_LOCAL': dis.haslocal,
        'HAS_FREE': dis.hasfree,
        'HAS_NARGS': getattr(dis, 'hasnargs', []), # Removed in Python 3.12
      }
      print(json.dumps(output))
    """))
    f.flush()
    proc1 = subprocess.run(
        [f'python{version1}', f.name],
        capture_output=True,
        text=True,
        check=True,
    )
    dis1 = json.loads(proc1.stdout)
    proc2 = subprocess.run(
        [f'python{version2}', f.name],
        capture_output=True,
        text=True,
        check=True,
    )
    dis2 = json.loads(proc2.stdout)

  # Diff the two opcode mappings, generating a change dictionary with entries:
  #   index: ('DELETE', deleted opcode)
  #   index: ('CHANGE', old opcode at this index, new opcode at this index)
  #   index: ('FLAG_CHANGE', opcode)
  #   index: ('ADD', new opcode)
  changed = {}
  impl_changed = set()
  name_unchanged = set()

  def is_unset(opname_entry):
    return opname_entry == f'<{i}>'

  for i in range(_MIN_INSTRUMENTED_OPCODE - 1):
    opname1 = dis1['opname'][i]
    opname2 = dis2['opname'][i]
    if opname1 == opname2:
      name_unchanged.add(i)
      continue
    if is_unset(opname2):
      changed[i] = ('DELETE', opname1)
    else:
      if opname2 in dis1['opmap']:
        impl_changed.add(opname2)
      if is_unset(opname1):
        changed[i] = ('ADD', opname2)
      else:
        changed[i] = ('CHANGE', opname1, opname2)

  # Detect flag changes
  for i in name_unchanged:
    for k in set(dis1).union(dis2):
      if not k.startswith('HAS_'):
        continue
      has_flag1 = k in dis1 and i in dis1[k]
      has_flag2 = k in dis2 and i in dis2[k]
      if has_flag1 != has_flag2:
        opname = dis1['opname'][i]
        impl_changed.add(opname)
        changed[i] = ('FLAG_CHANGE', opname)

  # Generate opcode classes.
  classes = []
  for op, diff in sorted(changed.items()):
    if diff[0] == 'DELETE':
      continue
    name = diff[-1]
    flags = []
    if op >= dis2['HAVE_ARGUMENT']:
      cls = [f'class {name}(OpcodeWithArg):']
      flags.append('HAS_ARGUMENT')
    else:
      cls = [f'class {name}(Opcode):']
    for k in dis2:
      if not k.startswith('HAS_'):
        continue
      if op not in dis2[k]:
        continue
      flags.append(k)
    if flags:
      cls.append('  _FLAGS = ' + ' | '.join(flags))
    cls.append('  __slots__ = ()')
    classes.append(cls)

  # Generate stub implementations.
  stubs = []
  for _, diff in sorted(changed.items()):
    if diff[0] == 'DELETE':
      continue
    name = diff[-1]
    if name in impl_changed:
      continue
    stubs.append(
        [f'def byte_{name}(self, state, op):', '  del op', '  return state']
    )

  # Generate a mapping diff.
  mapping = []
  for op, diff in sorted(changed.items()):
    if diff[0] == 'DELETE':
      name = diff[1]
      mapping.append(f'{op}: None,  # was {name} in {version1}')
    elif diff[0] == 'CHANGE':
      old_name, new_name = diff[1:]  # pytype: disable=bad-unpacking
      mapping.append(f'{op}: "{new_name}",  # was {old_name} in {version1}')
    elif diff[0] == 'ADD':
      name = diff[1]
      mapping.append(f'{op}: "{name}",')
    else:
      assert diff[0] == 'FLAG_CHANGE'

  # Generate arg type diff
  arg_types = []
  for _, diff in sorted(changed.items()):
    if diff[0] == 'DELETE':
      continue
    name = diff[-1]
    old_type = _get_arg_type(dis1, name)
    new_type = _get_arg_type(dis2, name)
    if new_type != old_type:
      arg_types.append(f'"{name}": {new_type},')

  # Intrinsic call descriptions
  v2 = version2.replace('.', '_')
  intrinsic_1_descs, intrinsic_1_stubs = _diff_intrinsic_descs(
      dis1['intrinsic_1_descs'], dis2['intrinsic_1_descs'], v2
  )
  intrinsic_2_descs, intrinsic_2_stubs = _diff_intrinsic_descs(
      dis1['intrinsic_2_descs'], dis2['intrinsic_2_descs'], v2
  )
  for stub in intrinsic_1_stubs:
    stubs.append(stub)
  for stub in intrinsic_2_stubs:
    stubs.append(stub)

  # Generate inline cache entries diff
  inline_cache_entries = []
  for name in dis2['opname']:
    old_entries = _get_inline_cache_entries(dis1, name)
    new_entries = _get_inline_cache_entries(dis2, name)
    if old_entries != new_entries:
      inline_cache_entries.append(f'"{name}": {new_entries},')

  return (
      classes,
      stubs,
      sorted(impl_changed),
      mapping,
      arg_types,
      intrinsic_1_descs,
      intrinsic_2_descs,
      inline_cache_entries,
  )


def _get_arg_type(dis, opname):
  all_types = ['CONST', 'NAME', 'JREL', 'JABS', 'LOCAL', 'FREE', 'NARGS']
  for t in all_types:
    k = f'HAS_{t}'
    if k in dis and opname in dis['opmap'] and dis['opmap'][opname] in dis[k]:
      return t
  return None


def _diff_intrinsic_descs(old, new, new_version):
  """Diff intrinsic descriptions and returns mapping and stubs if they differ."""
  if old == new:
    return [], []
  mapping = (
      [f'PYTHON_{new_version}_INTRINSIC_1_DESCS = [']
      + [f'    "{name}",' for name in new]
      + [']']
  )
  stubs = []
  for name in new:
    if name not in old:
      stubs.append([f'def byte_{name}(self, state):', '  return state'])
  return mapping, stubs


def _get_inline_cache_entries(dis, opname):
  if opname not in dis['opmap']:
    return 0
  op = dis['opmap'][opname]
  return (
      dis['inline_cache_entries'][op]
      if len(dis['inline_cache_entries']) > op
      else 0
  )


def main(argv):
  (
      classes,
      stubs,
      impl_changed,
      mapping,
      arg_types,
      intrinsic_1_descs,
      intrinsic_2_descs,
      inline_cache_entries,
  ) = generate_diffs(argv)
  print('==== PYTYPE CHANGES ====\n')
  print('---- NEW OPCODES (pyc/opcodes.py) ----\n')
  print('\n\n\n'.join('\n'.join(cls) for cls in classes))
  if impl_changed:
    print(
        '\nNOTE: Delete the old class definitions for the following '
        'modified opcodes: '
        + ', '.join(impl_changed)
    )
  print('\n---- OPCODE STUB IMPLEMENTATIONS (vm.py) ----\n')
  print('\n\n'.join('  ' + '\n  '.join(stub) for stub in stubs))
  if impl_changed:
    print(
        '\nNOTE: The implementations of the following modified opcodes may '
        'need to be updated: '
        + ', '.join(impl_changed)
    )

  print('\n\n==== PYCNITE CHANGES ====\n')
  print('---- OPCODE MAPPING DIFF (mapping.py) ----\n')
  print('    ' + '\n    '.join(mapping))
  print('\n---- OPCODE ARG TYPE DIFF (mapping.py) ----\n')
  print('    ' + '\n    '.join(arg_types))
  print('\n---- OPCODE INTRINSIC DESCS DIFF (mapping.py) ----\n')
  print('    ' + '\n    '.join(intrinsic_1_descs))
  print('    ' + '\n    '.join(intrinsic_2_descs))
  print('\n---- OPCODE INLINE CACHE ENTRIES DIFF (mapping.py) ----\n')
  print('    ' + '\n    '.join(inline_cache_entries))


if __name__ == '__main__':
  main(sys.argv[1:])
