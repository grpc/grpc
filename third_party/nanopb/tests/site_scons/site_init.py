import subprocess
import sys
import re

try:
    # Make terminal colors work on windows
    import colorama
    colorama.init()
except ImportError:
    pass

def add_nanopb_builders(env):
    '''Add the necessary builder commands for nanopb tests.'''

    # Build command that runs a test program and saves the output
    def run_test(target, source, env):
        if len(source) > 1:
            infile = open(str(source[1]))
        else:
            infile = None
        
        if env.has_key("COMMAND"):
            args = [env["COMMAND"]]
        else:
            args = [str(source[0])]
        
        if env.has_key('ARGS'):
            args.extend(env['ARGS'])
        
        print 'Command line: ' + str(args)
        pipe = subprocess.Popen(args,
                                stdin = infile,
                                stdout = open(str(target[0]), 'w'),
                                stderr = sys.stderr)
        result = pipe.wait()
        if result == 0:
            print '\033[32m[ OK ]\033[0m   Ran ' + args[0]
        else:
            print '\033[31m[FAIL]\033[0m   Program ' + args[0] + ' returned ' + str(result)
        return result
        
    run_test_builder = Builder(action = run_test,
                               suffix = '.output')
    env.Append(BUILDERS = {'RunTest': run_test_builder})

    # Build command that decodes a message using protoc
    def decode_actions(source, target, env, for_signature):
        esc = env['ESCAPE']
        dirs = ' '.join(['-I' + esc(env.GetBuildPath(d)) for d in env['PROTOCPATH']])
        return '$PROTOC $PROTOCFLAGS %s --decode=%s %s <%s >%s' % (
            dirs, env['MESSAGE'], esc(str(source[1])), esc(str(source[0])), esc(str(target[0])))

    decode_builder = Builder(generator = decode_actions,
                             suffix = '.decoded')
    env.Append(BUILDERS = {'Decode': decode_builder})    

    # Build command that encodes a message using protoc
    def encode_actions(source, target, env, for_signature):
        esc = env['ESCAPE']
        dirs = ' '.join(['-I' + esc(env.GetBuildPath(d)) for d in env['PROTOCPATH']])
        return '$PROTOC $PROTOCFLAGS %s --encode=%s %s <%s >%s' % (
            dirs, env['MESSAGE'], esc(str(source[1])), esc(str(source[0])), esc(str(target[0])))

    encode_builder = Builder(generator = encode_actions,
                             suffix = '.encoded')
    env.Append(BUILDERS = {'Encode': encode_builder})    

    # Build command that asserts that two files be equal
    def compare_files(target, source, env):
        data1 = open(str(source[0]), 'rb').read()
        data2 = open(str(source[1]), 'rb').read()
        if data1 == data2:
            print '\033[32m[ OK ]\033[0m   Files equal: ' + str(source[0]) + ' and ' + str(source[1])
            return 0
        else:
            print '\033[31m[FAIL]\033[0m   Files differ: ' + str(source[0]) + ' and ' + str(source[1])
            return 1

    compare_builder = Builder(action = compare_files,
                              suffix = '.equal')
    env.Append(BUILDERS = {'Compare': compare_builder})

    # Build command that checks that each pattern in source2 is found in source1.
    def match_files(target, source, env):
        data = open(str(source[0]), 'rU').read()
        patterns = open(str(source[1]))
        for pattern in patterns:
            if pattern.strip():
                invert = False
                if pattern.startswith('! '):
                    invert = True
                    pattern = pattern[2:]
                
                status = re.search(pattern.strip(), data, re.MULTILINE)
                
                if not status and not invert:
                    print '\033[31m[FAIL]\033[0m   Pattern not found in ' + str(source[0]) + ': ' + pattern
                    return 1
                elif status and invert:
                    print '\033[31m[FAIL]\033[0m   Pattern should not exist, but does in ' + str(source[0]) + ': ' + pattern
                    return 1
        else:
            print '\033[32m[ OK ]\033[0m   All patterns found in ' + str(source[0])
            return 0

    match_builder = Builder(action = match_files, suffix = '.matched')
    env.Append(BUILDERS = {'Match': match_builder})
    

