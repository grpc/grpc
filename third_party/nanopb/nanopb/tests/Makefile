all:
	scons

clean:
	scons -c

coverage:
	rm -rf build coverage

	# LCOV does not like the newer gcov format
	scons CC=gcc-4.6 CXX=gcc-4.6

	# Collect the data
	mkdir build/coverage
	lcov --base-directory . --directory build/ --gcov-tool gcov-4.6 -c -o build/coverage/nanopb.info

	# Remove the test code from results
	lcov -r build/coverage/nanopb.info '*tests*' -o build/coverage/nanopb.info

	# Generate HTML
	genhtml -o build/coverage build/coverage/nanopb.info
