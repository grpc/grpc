package goshared

const hostTpl = `
	func (m {{ (msgTyp .).Pointer }}) _validateHostname(host string) error {
		s := strings.ToLower(strings.TrimSuffix(host, "."))

		if len(host) > 253 {
			return errors.New("hostname cannot exceed 253 characters")
		}

		for _, part := range strings.Split(s, ".") {
			if l := len(part); l == 0 || l > 63 {
				return errors.New("hostname part must be non-empty and cannot exceed 63 characters")
			}

			if part[0] == '-' {
				return errors.New("hostname parts cannot begin with hyphens")
			}

			if part[len(part)-1] == '-' {
				return errors.New("hostname parts cannot end with hyphens")
			}

			for _, r := range part {
				if (r < 'a' || r > 'z') && (r < '0' || r > '9') && r != '-' {
					return fmt.Errorf("hostname parts can only contain alphanumeric characters or hyphens, got %q", string(r))
				}
			}
		}

		return nil
	}
`

const emailTpl = `
	func (m {{ (msgTyp .).Pointer }}) _validateEmail(addr string) error {
		a, err := mail.ParseAddress(addr)
		if err != nil {
			return err
		}
		addr = a.Address

		if len(addr) > 254 {
			return errors.New("email addresses cannot exceed 254 characters")
		}

		parts := strings.SplitN(addr, "@", 2)

		if len(parts[0]) > 64 {
			return errors.New("email address local phrase cannot exceed 64 characters")
		}

		return m._validateHostname(parts[1])
	}
`

const uuidTpl = `
	func (m {{ (msgTyp .).Pointer }}) _validateUuid(uuid string) error {
		if matched := _{{ snakeCase .File.InputPath.BaseName }}_uuidPattern.MatchString(uuid); !matched {
			return errors.New("invalid uuid format")
		}

		return nil
	}
`
