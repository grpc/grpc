package cc

const wrapperTpl = `
	{{ $f := .Field }}{{ $r := .Rules }}

	if ({{ hasAccessor . }}) {
		const auto wrapped = {{ accessor . }};
		{{ render (unwrap . "wrapped") }}
	} {{ if .MessageRules.GetRequired }} else {
		{{ err . "value is required and must not be nil." }}
	} {{ end }}
`
