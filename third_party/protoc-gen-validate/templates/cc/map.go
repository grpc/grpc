package cc

const mapTpl = `
	{{ $f := .Field }}{{ $r := .Rules }}

	{{ if $r.GetMinPairs }}
		{
		const auto size = {{ accessor . }}.size();
		{{ if eq $r.GetMinPairs $r.GetMaxPairs }}
			if (size != {{ $r.GetMinPairs }}) {
				{{ err . "value must contain exactly " $r.GetMinPairs " pair(s)" }}
			}
		{{ else if $r.MaxPairs }}
			if (size < {{ $r.GetMinPairs }} || size > {{ $r.GetMaxPairs }}) {
				{{ err . "value must contain between " $r.GetMinPairs " and " $r.GetMaxPairs " pairs, inclusive" }}
			}
		{{ else }}
			if (size < {{ $r.GetMinPairs }}) {
				{{ err . "value must contain at least " $r.GetMinPairs " pair(s)" }}
			}
		{{ end }}
	}
	{{ else if $r.MaxPairs }}
		{
		const auto size = {{ accessor . }}.size();
		if (size > {{ $r.GetMaxPairs }}) {
			{{ err . "value must contain no more than " $r.GetMaxPairs " pair(s)" }}
		}
	}
	{{ end }}

	{{ if or $r.GetNoSparse (ne (.Elem "" "").Typ "none") (ne (.Key "" "").Typ "none") }}
		for (const auto& kv : {{ accessor . }}) {
			const auto& key = kv.first;
			const auto& val = kv.second;
			(void)key;
			(void)val;

			{{ render (.Key "key" "key") }}

			{{ render (.Elem "val" "key") }}
		}

		{{ if $r.GetNoSparse }}
			{{ unimplemented "no_sparse validation is not implemented for C++ because protobuf maps cannot be sparse in C++" }}
		{{ end }}
	{{ end }}
`
