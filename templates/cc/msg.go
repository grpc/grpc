package cc

const declTpl = `
{{ if not (ignored .) -}}
extern bool Validate(const {{ class . }}& m, pgv::ValidationMsg* err);
{{- end -}}
`

const msgTpl = `
{{ if not (ignored .) -}}
{{ if disabled . -}}
	{{ cmt "Validate is disabled for " (class .) ". This method will always return true." }}
{{- else -}}
	{{ cmt "Validate checks the field values on " (class .) " with the rules defined in the proto definition for this message. If any rules are violated, the return value is false and an error message is written to the input string argument." }}
{{- end -}}

{{ range .Fields }}{{ with (context .) }}{{ $f := .Field }}
	{{ if has .Rules "In" }}{{ if .Rules.In }}
	const std::set<{{ inType .Field .Rules.In }}> {{ lookup .Field "InLookup" }} = {
			{{- range .Rules.In }}
				{{ inKey $f . }},
			{{- end }}
		};
	{{ end }}{{ end }}

	{{ if has .Rules "NotIn" }}{{ if .Rules.NotIn }}
	const std::set<{{ inType .Field .Rules.NotIn }}> {{ lookup .Field "NotInLookup" }} = {
			{{- range .Rules.NotIn }}
				{{ inKey $f . }},
			{{- end }}
		};
	{{ end }}{{ end }}

	{{ if has .Rules "Items"}}{{ if .Rules.Items }}
	{{ if has .Rules.Items.GetString_ "In" }} {{ if .Rules.Items.GetString_.In }}
	const std::set<string> {{ lookup .Field "InLookup" }} = {
			{{- range .Rules.Items.GetString_.In }}
				{{ inKey $f . }},
			{{- end }}
		};
	{{ end }}{{ end }}
	{{ if has .Rules.Items.GetEnum "In" }} {{ if .Rules.Items.GetEnum.In }}
	const std::set<{{ inType .Field .Rules.Items.GetEnum.In }}> {{ lookup .Field "InLookup" }} = {
			{{- range .Rules.Items.GetEnum.In }}
				{{ inKey $f . }},
			{{- end }}
		};
	{{ end }}{{ end }}
	{{ end }}{{ end }}

	{{ if has .Rules "Items"}}{{ if .Rules.Items }}
	{{ if has .Rules.Items.GetString_ "NotIn" }} {{ if .Rules.Items.GetString_.NotIn }}
	const std::set<string> {{ lookup .Field "NotInLookup" }} = {
			{{- range .Rules.Items.GetString_.NotIn }}
				{{ inKey $f . }},
			{{- end }}
		};
	{{ end }}{{ end }}
	{{ if has .Rules.Items.GetEnum "NotIn" }} {{ if .Rules.Items.GetEnum.NotIn }}
	const std::set<{{ inType .Field .Rules.Items.GetEnum.NotIn }}> {{ lookup .Field "NotInLookup" }} = {
			{{- range .Rules.Items.GetEnum.NotIn }}
				{{ inKey $f . }},
			{{- end }}
		};
	{{ end }}{{ end }}
	{{ end }}{{ end }}

        {{ if has .Rules "Pattern"}}{{ if .Rules.Pattern }}
               const re2::RE2 {{ lookup .Field "Pattern" }}(re2::StringPiece({{ lit .Rules.GetPattern }},
                                                            sizeof({{ lit .Rules.GetPattern }}) - 1));
	{{ end }}{{ end }}

	{{ if has .Rules "Items"}}{{ if .Rules.Items }}
        {{ if has .Rules.Items.GetString_ "Pattern" }} {{ if .Rules.Items.GetString_.Pattern }}
               const re2::RE2 {{ lookup .Field "Pattern" }}(re2::StringPiece({{ lit .Rules.Items.GetString_.GetPattern }},
                                              sizeof({{ lit .Rules.Items.GetString_.GetPattern }}) - 1));
	{{ end }}{{ end }}
        {{ end }}{{ end }}

        {{ if has .Rules "Keys"}}{{ if .Rules.Keys }}
	{{ if has .Rules.Keys.GetString_ "Pattern" }} {{ if .Rules.Keys.GetString_.Pattern }}
		const re2::RE2 {{ lookup .Field "Pattern" }}(re2::StringPiece({{ lit .Rules.Keys.GetString_.GetPattern }},
                                              sizeof({{ lit .Rules.Keys.GetString_.GetPattern }}) - 1));
	{{ end }}{{ end }}
	{{ end }}{{ end }}

	{{ if has .Rules "Values"}}{{ if .Rules.Values }}
	{{ if has .Rules.Values.GetString_ "Pattern" }} {{ if .Rules.Values.GetString_.Pattern }}
		const re2::RE2 {{ lookup .Field "Pattern" }}(re2::StringPiece({{ lit .Rules.Values.GetString_.GetPattern }},
                                              sizeof({{ lit .Rules.Values.GetString_.GetPattern }}) - 1));
	{{ end }}{{ end }}
	{{ end }}{{ end }}

{{ end }}{{ end }}

bool Validate(const {{ class . }}& m, pgv::ValidationMsg* err) {
	(void)m;
	(void)err;
{{- if disabled . }}
	return true;
{{ else -}}
		{{ range .NonOneOfFields }}
			{{- render (context .) -}}
		{{ end -}}
		{{ range .OneOfs }}
			switch (m.{{ .Name }}_case()) {
				{{ range .Fields -}}
					case {{ oneof . }}:
						{{ render (context .) }}
						break;
				{{ end -}}
					default:
				{{- if required . }}
						*err = "field: " {{ .Name | quote | lit }} ", reason: is required";
						return false;
				{{ end }}
					break;
			}
		{{ end }}
	return true;
{{ end -}}
}
{{- end -}}
`
