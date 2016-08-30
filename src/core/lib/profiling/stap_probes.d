provider _stap {
	probe add_mark(int tag);
	probe add_important_mark(int tag);
	probe timing_ns_begin(int tag);
	probe timing_ns_end(int tag);
};

