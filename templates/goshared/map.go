package goshared

const mapTpl = `
	{{ $f := .Field }}{{ $r := .Rules }}

	{{ if $r.GetMinPairs }}
		{{ if eq $r.GetMinPairs $r.GetMaxPairs }}
			if len({{ accessor . }}) != {{ $r.GetMinPairs }} {
				return {{ err . "value must contain exactly " $r.GetMinPairs " pair(s)" }}
			}
		{{ else if $r.MaxPairs }}
			if l := len({{ accessor . }}); l < {{ $r.GetMinPairs }} || l > {{ $r.GetMaxPairs }} {
			 	return {{ err . "value must contain between " $r.GetMinPairs " and " $r.GetMaxPairs " pairs, inclusive" }}
			}
		{{ else }}
			if len({{ accessor . }}) < {{ $r.GetMinPairs }} {
				return {{ err . "value must contain at least " $r.GetMinPairs " pair(s)" }}
			}
		{{ end }}
	{{ else if $r.MaxPairs }}
		if len({{ accessor . }}) > {{ $r.GetMaxPairs }} {
			return {{ err . "value must contain no more than " $r.GetMaxPairs " pair(s)" }}
		}
	{{ end }}

	{{ if or $r.GetNoSparse (ne (.Elem "" "").Typ "none") (ne (.Key "" "").Typ "none") }}
		for key, val := range {{ accessor . }} {
			_ = val

			{{ if $r.GetNoSparse }}
				if val == nil {
					return {{ errIdx . "key" "value cannot be sparse, all pairs must be non-nil" }}
				}
			{{ end }}

			{{ render (.Key "key" "key") }}

			{{ render (.Elem "val" "key") }}
		}
	{{ end }}
`
