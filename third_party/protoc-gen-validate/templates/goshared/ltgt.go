package goshared

const ltgtTpl = `{{ $f := .Field }}{{ $r := .Rules }}
	{{ if $r.Lt }}
		{{ if $r.Gt }}
			{{  if gt $r.GetLt $r.GetGt }}
				if val := {{ accessor . }};  val <= {{ $r.Gt }} || val >= {{ $r.Lt }} {
					return {{ err . "value must be inside range (" $r.GetGt ", " $r.GetLt ")" }}
				}
			{{ else }}
				if val := {{ accessor . }}; val >= {{ $r.Lt }} && val <= {{ $r.Gt }} {
					return {{ err . "value must be outside range [" $r.GetLt ", " $r.GetGt "]" }}
				}
			{{ end }}
		{{ else if $r.Gte }}
			{{  if gt $r.GetLt $r.GetGte }}
				if val := {{ accessor . }};  val < {{ $r.Gte }} || val >= {{ $r.Lt }} {
					return {{ err . "value must be inside range [" $r.GetGte ", " $r.GetLt ")" }}
				}
			{{ else }}
				if val := {{ accessor . }}; val >= {{ $r.Lt }} && val < {{ $r.Gte }} {
					return {{ err . "value must be outside range [" $r.GetLt ", " $r.GetGte ")" }}
				}
			{{ end }}
		{{ else }}
			if {{ accessor . }} >= {{ $r.Lt }} {
				return {{ err . "value must be less than " $r.GetLt }}
			}
		{{ end }}
	{{ else if $r.Lte }}
		{{ if $r.Gt }}
			{{  if gt $r.GetLte $r.GetGt }}
				if val := {{ accessor . }};  val <= {{ $r.Gt }} || val > {{ $r.Lte }} {
					return {{ err . "value must be inside range (" $r.GetGt ", " $r.GetLte "]" }}
				}
			{{ else }}
				if val := {{ accessor . }}; val > {{ $r.Lte }} && val <= {{ $r.Gt }} {
					return {{ err . "value must be outside range (" $r.GetLte ", " $r.GetGt "]" }}
				}
			{{ end }}
		{{ else if $r.Gte }}
			{{ if gt $r.GetLte $r.GetGte }}
				if val := {{ accessor . }};  val < {{ $r.Gte }} || val > {{ $r.Lte }} {
					return {{ err . "value must be inside range [" $r.GetGte ", " $r.GetLte "]" }}
				}
			{{ else }}
				if val := {{ accessor . }}; val > {{ $r.Lte }} && val < {{ $r.Gte }} {
					return {{ err . "value must be outside range (" $r.GetLte ", " $r.GetGte ")" }}
				}
			{{ end }}
		{{ else }}
			if {{ accessor . }} > {{ $r.Lte }} {
				return {{ err . "value must be less than or equal to " $r.GetLte }}
			}
		{{ end }}
	{{ else if $r.Gt }}
		if {{ accessor . }} <= {{ $r.Gt }} {
			return {{ err . "value must be greater than " $r.GetGt }}
		}
	{{ else if $r.Gte }}
		if {{ accessor . }} < {{ $r.Gte }} {
			return {{ err . "value must be greater than or equal to " $r.GetGte }}
		}
	{{ end }}
`
