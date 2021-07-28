EXCLUDE_FROM_ALL
----------------

Set this directory property to a true value on a subdirectory to exclude
its targets from the "all" target of its ancestors.  If excluded, running
e.g. ``make`` in the parent directory will not build targets the
subdirectory by default.  This does not affect the "all" target of the
subdirectory itself.  Running e.g. ``make`` inside the subdirectory will
still build its targets.

If the :prop_tgt:`EXCLUDE_FROM_ALL` target property is set on a target
then its value determines whether the target is included in the "all"
target of this directory and its ancestors.
