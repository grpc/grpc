The bm_diff Family
====

This family of python scripts can be incredibly useful for fast iteration over
different performance tweaks. The tools allow you to save performance data from
a baseline commit, then quickly compare data from your working branch to that
baseline data to see if you have made any performance wins.

The tools operate with three concrete steps, which can be invoked separately,
or all together via the driver script, bm_main.py. This readme will describe 
the typical workflow for these scripts, then it will include sections on the
details of every script for advanced usage.

## Normal Workflow

Let's say you are working on a performance optimization for grpc_error. You have
made some significant changes and want to see some data. From your branch, run
(ensure everything is committed first):

`tools/profiling/microbenchmarks/bm_diff/bm_main.py -b bm_error -l 5 -d master`

This will build the `bm_error` binary on your branch, and then it will checkout 
master and build it there too. It will then run these benchmarks 5 times each. 
Lastly it will compute the statistically significant performance differences 
between the two branches. This should show the nice performance wins your 
changes have made.

If you have already invoked bm_main with `-d master`, you should instead use 
`-o` for subsequent runs. This allows the script to skip re-building and 
re-running the unchanged master branch. For example:

`tools/profiling/microbenchmarks/bm_diff/bm_main.py -b bm_error -l 5 -o`

This will only build and run `bm_error` on your branch. It will then compare
the output to the saved runs from master.

## Advanced Workflow

If you have a deeper knowledge of these scripts, you can use them to do more
fine tuned benchmark comparisons. For example, you could build, run, and save
the benchmark output from two different base branches. Then you could diff both
of these baselines against your working branch to see how the different metrics
change. The rest of this doc goes over the details of what each of the
individual modules accomplishes.

## bm_build.py

This scrips builds the benchmarks. It takes in a name parameter, and will
store the binaries based on that. Both `opt` and `counter` configurations
will be used. The `opt` is used to get cpu_time and real_time, and the
`counters` build is used to track other metrics like allocs, atomic adds,
etc etc etc.

For example, if you were to invoke (we assume everything is run from the 
root of the repo):

`tools/profiling/microbenchmarks/bm_diff/bm_build.py -b bm_error -n baseline`

then the microbenchmark binaries will show up under 
`bm_diff_baseline/{opt,counters}/bm_error`

## bm_run.py

This script runs the benchmarks. It takes a name parameter that must match the
name that was passed to `bm_build.py`. The script then runs the benchmark
multiple times (default is 20, can be toggled via the loops parameter). The
output is saved as `<benchmark name>.<config>.<name>.<loop idx>.json`

For example, if you were to run:

`tools/profiling/microbenchmarks/bm_diff/bm_run.py -b bm_error -b baseline -l 5`

Then an example output file would be `bm_error.opt.baseline.0.json`

## bm_diff.py

This script takes in the output from two benchmark runs, computes the diff
between them, and prints any significant improvements or regressions. It takes
in two name parameters, old and new. These must have previously been built and
run.

For example, assuming you had already built and run a 'baseline' microbenchmark
from master, and then you also built and ran a 'current' microbenchmark from
the branch you were working on, you could invoke:

`tools/profiling/microbenchmarks/bm_diff/bm_diff.py -b bm_error -o baseline -n current -l 5`

This would output the percent difference between your branch and master.

## bm_main.py

This is the driver script. It uses the previous three modules and does
everything for you. You pass in the benchmarks to be run, the number of loops,
number of CPUs to use, and the commit to compare to. Then the script will:
* Build the benchmarks at head, then checkout the branch to compare to and
  build the benchmarks there
* Run both sets of microbenchmarks
* Run bm_diff.py to compare the two, outputs the difference.

For example, one might run:

`tools/profiling/microbenchmarks/bm_diff/bm_main.py -b bm_error -l 5 -d master`

This would compare the current branch's error benchmarks to master.

This script is invoked by our infrastructure on every PR to protect against
regressions and demonstrate performance wins.

However, if you are iterating over different performance tweaks quickly, it is
unnecessary to build and run the baseline commit every time. That is why we
provide a different flag in case you are sure that the baseline benchmark has
already been built and run. In that case use the --old flag to pass in the name
of the baseline. This will only build and run the current branch. For example:

`tools/profiling/microbenchmarks/bm_diff/bm_main.py -b bm_error -l 5 -o old`

