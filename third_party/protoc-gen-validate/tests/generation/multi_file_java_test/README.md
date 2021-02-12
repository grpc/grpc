# Multi File Java Test #

Test that a file with: `option java_multiple_files = true;` not generated in
java can properly be generated without error. Due to `java` not needing to be
in the generation path at all we had to split it out into it's own directory.
