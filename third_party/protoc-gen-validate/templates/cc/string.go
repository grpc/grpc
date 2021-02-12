package cc

const strTpl = `
	{{ $f := .Field }}{{ $r := .Rules }}
	{{ template "const" . }}
	{{ template "in" . }}
	{{ if or $r.Len (and $r.MinLen $r.MaxLen (eq $r.GetMinLen $r.GetMaxLen)) }}
		{{ if $r.Len }}
			if (pgv::Utf8Len({{ accessor . }}) != {{ $r.GetLen }}) {
				{{ err . "value must be " $r.GetLen " runes" }}
			}
		{{ else }}
			if (pgv::Utf8Len({{ accessor . }}) != {{ $r.GetMinLen }}) {
				{{ err . "value must be " $r.GetMinLen " runes" }}
			}
		{{ end }}
	{{ else if $r.MinLen }}
		{{ if $r.MaxLen }}
			{
				const auto length = pgv::Utf8Len({{ accessor . }});
				if (length < {{ $r.GetMinLen }} || length > {{ $r.GetMaxLen }}) {
					{{ err . "value must have between " $r.GetMinLen " and " $r.GetMaxLen " runes inclusive" }}
				}
			}
		{{ else }}
			if (pgv::Utf8Len({{ accessor . }}) < {{ $r.GetMinLen }}) {
				{{ err . "value length must be at least " $r.GetMinLen " runes" }}
			}
		{{ end }}
	{{ else if $r.MaxLen }}
		if (pgv::Utf8Len({{ accessor . }}) > {{ $r.GetMaxLen }}) {
			{{ err . "value length must be at most " $r.GetMaxLen " runes" }}
		}
	{{ end }}

	{{ if or $r.LenBytes (and $r.MinBytes $r.MaxBytes (eq $r.GetMinBytes $r.GetMaxBytes)) }}
	{
		const auto length = {{ accessor . }}.size();
		{{ if $r.LenBytes }}
			if (length != {{ $r.GetLenBytes }}) {
				{{ err . "value length must be " $r.GetLenBytes " bytes" }}
			}
		{{ else }}
			if (length != {{ $r.GetMinBytes }}) {
				{{ err . "value length must be " $r.GetMinBytes " bytes" }}
			}
		{{ end }}
	}
	{{ else if $r.MinBytes }}
	{
		const auto length = {{ accessor . }}.size();
		{{ if $r.MaxBytes }}
			{{ if eq $r.GetMinBytes $r.GetMaxBytes }}
				if (length != {{ $r.GetMinBytes }}) {
					{{ err . "value length must be " $r.GetMinBytes " bytes" }}
				}
			{{ else }}
				if (length < {{ $r.GetMinBytes }} || length > {{ $r.GetMaxBytes }}) {
					{{ err . "value length must be between " $r.GetMinBytes " and " $r.GetMaxBytes " bytes, inclusive" }}
				}
			{{ end }}
		{{ else }}
			if (length < {{ $r.GetMinBytes }}) {
				{{ err . "value length must be at least " $r.GetMinBytes " bytes" }}
			}
		{{ end }}
	}
	{{ else if $r.MaxBytes }}
		if ({{ accessor . }}.size() > {{ $r.GetMaxBytes }}) {
			{{ err . "value length must be at most " $r.GetMaxBytes " bytes" }}
		}
	{{ end }}

	{{ if $r.Prefix }}
	{
		const std::string prefix = {{ lit $r.GetPrefix }};
		if (!pgv::IsPrefix(prefix, {{ accessor . }})) {
			{{ err . "value does not have prefix " (lit $r.GetPrefix) }}
		}
	}
	{{ end }}

	{{ if $r.Suffix }}
	{
		const std::string suffix = {{ lit $r.GetSuffix }};
		const std::string& value = {{ accessor . }};
		if (!pgv::IsSuffix(suffix, value)) {
			{{ err . "value does not have suffix " (lit $r.GetSuffix) }}
		}
	}
	{{ end }}

	{{ if $r.Contains }}
	{
		if (!pgv::Contains({{ accessor . }}, {{ lit $r.GetContains }})) {
			{{ err . "value does not contain substring " (lit $r.GetContains) }}
		}
	}
        {{ end }}

	{{ if $r.NotContains }}
	{
		if (pgv::Contains({{ accessor . }}, {{ lit $r.GetNotContains }})) {
			{{ err . "value contains substring " (lit $r.GetNotContains) }}
		}
	}
	{{ end }}

        {{ if $r.Pattern }}
        {
                if (!RE2::FullMatch(re2::StringPiece({{ accessor . }}.c_str(), {{ accessor . }}.size()),
                                    {{ lookup $f "Pattern" }})) {
		        {{ err . "value does not match regex pattern " (lit $r.GetPattern) }}
	        }
        }
	{{ end }}

	{{ if $r.GetIp }}
	{
		const std::string& value = {{ accessor . }};
		if (!pgv::IsIp(value)) {
			{{ err . "value must be a valid IP Address" }}
		}
	}
	{{ else if $r.GetIpv4 }}
	{
		const std::string& value = {{ accessor . }};
		if (!pgv::IsIpv4(value)) {
			{{ err . "value must be a valid IPv4 Address" }}
		}
	}
	{{ else if $r.GetIpv6 }}
	{
		const std::string& value = {{ accessor . }};
		if (!pgv::IsIpv6(value)) {
			{{ err . "value must be a valid IPv6 Address" }}
		}
	}
	{{ else if $r.GetEmail }}
		{{ unimplemented "C++ email address validation is not implemented" }}
		{{/* TODO(akonradi) implement email address constraints
		if err := m._validateEmail({{ accessor . }}); err != nil {
			return {{ errCause . "err" "value must be a valid email address" }}
		}
		*/}}
	{{ else if $r.GetAddress }}
	{
		const std::string& value = {{ accessor . }};

		if (!pgv::IsHostname(value) && !pgv::IsIp(value)) {
			{{ err . "value must be an ip address, or a hostname." }}
		}
	}
	{{ else if $r.GetHostname }}
	{
		const std::string& value = {{ accessor . }};

		if (!pgv::IsHostname(value)) {
			{{ err . "value must be a valid hostname" }}
		}
	}
	{{ else if $r.GetUri }}
		{{ unimplemented "C++ URI validation is not implemented" }}
		{{/* TODO(akonradi) implement URI constraints
		if uri, err := url.Parse({{ accessor . }}); err != nil {
			return {{ errCause . "err" "value must be a valid URI" }}
		} else if !uri.IsAbs() {
			return {{ err . "value must be absolute" }}
		}
		*/}}
	{{ else if $r.GetUriRef }}
		{{ unimplemented "C++ URI validation is not implemented" }}
		{{/* TODO(akonradi) implement URI constraints
		if _, err := url.Parse({{ accessor . }}); err != nil {
			return {{ errCause . "err" "value must be a valid URI" }}
		}
		*/}}
	{{ else if $r.GetUuid }}
                if (!RE2::FullMatch(re2::StringPiece({{ accessor . }}), pgv::validate::_uuidPattern)) {
                        {{ err . "value must be a valid UUID" }}
                }
	{{ end }}
`
