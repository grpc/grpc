import os
import sys
import re

def repl_fn(m):
  ret = ''
  ret = ret + m.groups()[0] + '('
  for i in range(1, len(m.groups())):
    if(m.groups()[i] != None):
      ret = ret + m.groups()[i]
    else:
      break
  ret = ret + ')'
  print '\n' + m.group() + '\nwith\n' + ret + '\n'
  return ret

def work_on(fname):
  with open(fname) as f:
    p = re.compile(r'((?:\b[^\s\(\),]+)|(?:\(\*[^\s\(\),]+\)))\s*' + # function name or function pointer
                   r'\(\s*' + # open brackets
                   r'(?:(?:exec_ctx)|(?:grpc_exec_ctx\s*\*\s*exec_ctx)|(?:&\s*exec_ctx))' + # first exec_ctx paramenter
                   r'\s*,?' + # comma after exec_ctx
                   r'(\s*[^\),]+)?' + # all but first argument
                   r'(\s*,\s*[^\),]+)?' + # all but first argument
                   r'(\s*,\s*[^\),]+)?' + # all but first argument
                   r'(\s*,\s*[^\),]+)?' + # all but first argument
                   r'(\s*,\s*[^\),]+)?' + # all but first argument
                   r'(\s*,\s*[^\),]+)?' + # all but first argument
                   r'(\s*,\s*[^\),]+)?' + # all but first argument
                   r'(\s*,\s*[^\),]+)?' + # all but first argument
                   r'(\s*,\s*[^\),]+)?' + # all but first argument
                   r'(\s*,\s*[^\),]+)?' + # all but first argument
                   r'(\s*,\s*[^\),]+)?' + # all but first argument
                   r'\s*\)') # close brackets
    res = p.sub(repl_fn, f.read())

    f = open(fname, 'w')
    f.write(res)
    f.close()
    #print res

def main():
  file_list = []
  for line in sys.stdin:
    work_on(line.strip())


if __name__ == '__main__':
  main()
