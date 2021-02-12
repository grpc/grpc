package cc

const durationTpl = `{{ $f := .Field }}{{ $r := .Rules }}
	{{ template "required" . }}

	{{ if or $r.In $r.NotIn $r.Lt $r.Lte $r.Gt $r.Gte $r.Const }}
	    {
	        if ({{ hasAccessor . }}) {
			const pgv::protobuf_wkt::Duration& dur = {{ accessor . }};

			if (dur.nanos() > 999999999 || dur.nanos() < -999999999 ||
			    dur.seconds() > pgv::protobuf::util::TimeUtil::kDurationMaxSeconds ||
			    dur.seconds() < pgv::protobuf::util::TimeUtil::kDurationMinSeconds)
				{{ errCause . "err" "value is not a valid duration" }}


			{{  if $r.Const }}
				if (dur != {{ durLit $r.Const }})
					{{ err . "value must equal " (durStr $r.Const) }}
			{{ end }}

			{{  if $r.Lt }}  const pgv::protobuf_wkt::Duration lt  = {{ durLit $r.Lt }};  {{ end }}
			{{- if $r.Lte }} const pgv::protobuf_wkt::Duration lte = {{ durLit $r.Lte }}; {{ end }}
			{{- if $r.Gt }}  const pgv::protobuf_wkt::Duration gt  = {{ durLit $r.Gt }};  {{ end }}
			{{- if $r.Gte }} const pgv::protobuf_wkt::Duration gte = {{ durLit $r.Gte }}; {{ end }}

			{{ if $r.Lt }}
				{{ if $r.Gt }}
					{{  if durGt $r.GetLt $r.GetGt }}
						if (dur <= gt || dur >= lt)
							{{ err . "value must be inside range (" (durStr $r.GetGt) ", " (durStr $r.GetLt) ")" }}
					{{ else }}
						if (dur >= lt && dur <= gt)
							{{ err . "value must be outside range [" (durStr $r.GetLt) ", " (durStr $r.GetGt) "]" }}
					{{ end }}
				{{ else if $r.Gte }}
					{{  if durGt $r.GetLt $r.GetGte }}
						if (dur < gte || dur >= lt)
							{{ err . "value must be inside range [" (durStr $r.GetGte) ", " (durStr $r.GetLt) ")" }}
					{{ else }}
						if (dur >= lt && dur < gte)
							{{ err . "value must be outside range [" (durStr $r.GetLt) ", " (durStr $r.GetGte) ")" }}
					{{ end }}
				{{ else }}
					if (dur >= lt)
						{{ err . "value must be less than " (durStr $r.GetLt) }}
				{{ end }}
			{{ else if $r.Lte }}
				{{ if $r.Gt }}
					{{  if durGt $r.GetLte $r.GetGt }}
						if (dur <= gt || dur > lte)
							{{ err . "value must be inside range (" (durStr $r.GetGt) ", " (durStr $r.GetLte) "]" }}
					{{ else }}
						if (dur > lte && dur <= gt)
							{{ err . "value must be outside range (" (durStr $r.GetLte) ", " (durStr $r.GetGt) "]" }}
					{{ end }}
				{{ else if $r.Gte }}
					{{ if durGt $r.GetLte $r.GetGte }}
						if (dur < gte || dur > lte)
							{{ err . "value must be inside range [" (durStr $r.GetGte) ", " (durStr $r.GetLte) "]" }}
					{{ else }}
						if (dur > lte && dur < gte)
							{{ err . "value must be outside range (" (durStr $r.GetLte) ", " (durStr $r.GetGte) ")" }}
					{{ end }}
				{{ else }}
					if (dur > lte)
						{{ err . "value must be less than or equal to " (durStr $r.GetLte) }}
				{{ end }}
			{{ else if $r.Gt }}
				if (dur <= gt)
					{{ err . "value must be greater than " (durStr $r.GetGt) }}
			{{ else if $r.Gte }}
				if (dur < gte)
					{{ err . "value must be greater than or equal to " (durStr $r.GetGte) }}
			{{ end }}


			{{ if $r.In }}
				if ({{ lookup $f "InLookup" }}.find(dur) == {{ lookup $f "InLookup" }}.end())
					{{ err . "value must be in list " $r.In }}
			{{ else if $r.NotIn }}
				if ({{ lookup $f "NotInLookup" }}.find(dur) != {{ lookup $f "NotInLookup" }}.end())
					{{ err . "value must not be in list " $r.NotIn }}
			{{ end }}
	        }
	    }
	{{ end }}
`
