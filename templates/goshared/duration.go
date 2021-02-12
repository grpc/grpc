package goshared

const durationcmpTpl = `{{ $f := .Field }}{{ $r := .Rules }}
			{{  if $r.Const }}
				if dur != {{ durLit $r.Const }} {
					return {{ err . "value must equal " (durStr $r.Const) }}
				}
			{{ end }}


			{{  if $r.Lt }}  lt  := {{ durLit $r.Lt }};  {{ end }}
			{{- if $r.Lte }} lte := {{ durLit $r.Lte }}; {{ end }}
			{{- if $r.Gt }}  gt  := {{ durLit $r.Gt }};  {{ end }}
			{{- if $r.Gte }} gte := {{ durLit $r.Gte }}; {{ end }}

			{{ if $r.Lt }}
				{{ if $r.Gt }}
					{{  if durGt $r.GetLt $r.GetGt }}
						if dur <= gt || dur >= lt {
							return {{ err . "value must be inside range (" (durStr $r.GetGt) ", " (durStr $r.GetLt) ")" }}
						}
					{{ else }}
						if dur >= lt && dur <= gt {
							return {{ err . "value must be outside range [" (durStr $r.GetLt) ", " (durStr $r.GetGt) "]" }}
						}
					{{ end }}
				{{ else if $r.Gte }}
					{{  if durGt $r.GetLt $r.GetGte }}
						if dur < gte || dur >= lt {
							return {{ err . "value must be inside range [" (durStr $r.GetGte) ", " (durStr $r.GetLt) ")" }}
						}
					{{ else }}
						if dur >= lt && dur < gte {
							return {{ err . "value must be outside range [" (durStr $r.GetLt) ", " (durStr $r.GetGte) ")" }}
						}
					{{ end }}
				{{ else }}
					if dur >= lt {
						return {{ err . "value must be less than " (durStr $r.GetLt) }}
					}
				{{ end }}
			{{ else if $r.Lte }}
				{{ if $r.Gt }}
					{{  if durGt $r.GetLte $r.GetGt }}
						if dur <= gt || dur > lte {
							return {{ err . "value must be inside range (" (durStr $r.GetGt) ", " (durStr $r.GetLte) "]" }}
						}
					{{ else }}
						if dur > lte && dur <= gt {
							return {{ err . "value must be outside range (" (durStr $r.GetLte) ", " (durStr $r.GetGt) "]" }}
						}
					{{ end }}
				{{ else if $r.Gte }}
					{{ if durGt $r.GetLte $r.GetGte }}
						if dur < gte || dur > lte {
							return {{ err . "value must be inside range [" (durStr $r.GetGte) ", " (durStr $r.GetLte) "]" }}
						}
					{{ else }}
						if dur > lte && dur < gte {
							return {{ err . "value must be outside range (" (durStr $r.GetLte) ", " (durStr $r.GetGte) ")" }}
						}
					{{ end }}
				{{ else }}
					if dur > lte {
						return {{ err . "value must be less than or equal to " (durStr $r.GetLte) }}
					}
				{{ end }}
			{{ else if $r.Gt }}
				if dur <= gt {
					return {{ err . "value must be greater than " (durStr $r.GetGt) }}
				}
			{{ else if $r.Gte }}
				if dur < gte {
					return {{ err . "value must be greater than or equal to " (durStr $r.GetGte) }}
				}
			{{ end }}


			{{ if $r.In }}
				if _, ok := {{ lookup $f "InLookup" }}[dur]; !ok {
					return {{ err . "value must be in list " $r.In }}
				}
			{{ else if $r.NotIn }}
				if _, ok := {{ lookup $f "NotInLookup" }}[dur]; ok {
					return {{ err . "value must not be in list " $r.NotIn }}
				}
			{{ end }}
`
