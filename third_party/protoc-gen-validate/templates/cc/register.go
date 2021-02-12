package cc

import (
	"fmt"
	"reflect"
	"strconv"
	"strings"
	"text/template"

	"github.com/envoyproxy/protoc-gen-validate/templates/shared"
	"github.com/golang/protobuf/ptypes"
	"github.com/golang/protobuf/ptypes/duration"
	"github.com/golang/protobuf/ptypes/timestamp"
	"github.com/iancoleman/strcase"
	pgs "github.com/lyft/protoc-gen-star"
	pgsgo "github.com/lyft/protoc-gen-star/lang/go"
)

func RegisterModule(tpl *template.Template, params pgs.Parameters) {
	fns := CCFuncs{pgsgo.InitContext(params)}

	tpl.Funcs(map[string]interface{}{
		"accessor":      fns.accessor,
		"byteStr":       fns.byteStr,
		"class":         fns.className,
		"cmt":           pgs.C80,
		"ctype":         fns.cType,
		"durGt":         fns.durGt,
		"durLit":        fns.durLit,
		"durStr":        fns.durStr,
		"err":           fns.err,
		"errCause":      fns.errCause,
		"errIdx":        fns.errIdx,
		"errIdxCause":   fns.errIdxCause,
		"hasAccessor":   fns.hasAccessor,
		"inKey":         fns.inKey,
		"inType":        fns.inType,
		"isBytes":       fns.isBytes,
		"lit":           fns.lit,
		"lookup":        fns.lookup,
		"oneof":         fns.oneofTypeName,
		"output":        fns.output,
		"package":       fns.packageName,
		"quote":         fns.quote,
		"staticVarName": fns.staticVarName,
		"tsGt":          fns.tsGt,
		"tsLit":         fns.tsLit,
		"tsStr":         fns.tsStr,
		"typ":           fns.Type,
		"unimplemented": fns.failUnimplemented,
		"unwrap":        fns.unwrap,
	})
	template.Must(tpl.Parse(moduleFileTpl))
	template.Must(tpl.New("msg").Parse(msgTpl))
	template.Must(tpl.New("const").Parse(constTpl))
	template.Must(tpl.New("ltgt").Parse(ltgtTpl))
	template.Must(tpl.New("in").Parse(inTpl))
	template.Must(tpl.New("required").Parse(requiredTpl))

	template.Must(tpl.New("none").Parse(noneTpl))
	template.Must(tpl.New("float").Parse(numTpl))
	template.Must(tpl.New("double").Parse(numTpl))
	template.Must(tpl.New("int32").Parse(numTpl))
	template.Must(tpl.New("int64").Parse(numTpl))
	template.Must(tpl.New("uint32").Parse(numTpl))
	template.Must(tpl.New("uint64").Parse(numTpl))
	template.Must(tpl.New("sint32").Parse(numTpl))
	template.Must(tpl.New("sint64").Parse(numTpl))
	template.Must(tpl.New("fixed32").Parse(numTpl))
	template.Must(tpl.New("fixed64").Parse(numTpl))
	template.Must(tpl.New("sfixed32").Parse(numTpl))
	template.Must(tpl.New("sfixed64").Parse(numTpl))

	template.Must(tpl.New("bool").Parse(constTpl))
	template.Must(tpl.New("string").Parse(strTpl))
	template.Must(tpl.New("bytes").Parse(bytesTpl))

	template.Must(tpl.New("email").Parse(emailTpl))
	template.Must(tpl.New("hostname").Parse(hostTpl))
	template.Must(tpl.New("address").Parse(hostTpl))
	template.Must(tpl.New("uuid").Parse(uuidTpl))

	template.Must(tpl.New("enum").Parse(enumTpl))
	template.Must(tpl.New("message").Parse(messageTpl))
	template.Must(tpl.New("repeated").Parse(repTpl))
	template.Must(tpl.New("map").Parse(mapTpl))

	template.Must(tpl.New("any").Parse(anyTpl))
	template.Must(tpl.New("duration").Parse(durationTpl))
	template.Must(tpl.New("timestamp").Parse(timestampTpl))

	template.Must(tpl.New("wrapper").Parse(wrapperTpl))
}

func RegisterHeader(tpl *template.Template, params pgs.Parameters) {
	fns := CCFuncs{pgsgo.InitContext(params)}

	tpl.Funcs(map[string]interface{}{
		"class":                fns.className,
		"output":               fns.output,
		"screaming_snake_case": strcase.ToScreamingSnake,
	})

	template.Must(tpl.Parse(headerFileTpl))
	template.Must(tpl.New("decl").Parse(declTpl))
}

// TODO(rodaine): break pgsgo dependency here (with equivalent pgscc subpackage)
type CCFuncs struct{ pgsgo.Context }

func CcFilePath(f pgs.File, ctx pgsgo.Context, tpl *template.Template) *pgs.FilePath {
	out := pgs.FilePath(f.Name().String())
	out = out.SetExt(".pb.validate." + tpl.Name())
	return &out
}

func (fns CCFuncs) methodName(name interface{}) string {
	nameStr := fmt.Sprintf("%s", name)
	switch nameStr {
	case "const":
		return "const_"
	case "inline":
		return "inline_"
	default:
		return nameStr
	}
}

func (fns CCFuncs) accessor(ctx shared.RuleContext) string {
	if ctx.AccessorOverride != "" {
		return ctx.AccessorOverride
	}

	return fmt.Sprintf(
		"m.%s()",
		fns.methodName(ctx.Field.Name()))
}

func (fns CCFuncs) hasAccessor(ctx shared.RuleContext) string {
	if ctx.AccessorOverride != "" {
		return "true"
	}

	return fmt.Sprintf(
		"m.has_%s()",
		fns.methodName(ctx.Field.Name()))
}

type childEntity interface {
	pgs.Entity
	Parent() pgs.ParentEntity
}

func (fns CCFuncs) classBaseName(ent childEntity) string {
	if m, ok := ent.Parent().(pgs.Message); ok {
		return fmt.Sprintf("%s_%s", fns.classBaseName(m), ent.Name().String())
	}
	return ent.Name().String()
}

func (fns CCFuncs) className(ent childEntity) string {
	return fns.packageName(ent) + "::" + fns.classBaseName(ent)
}

func (fns CCFuncs) packageName(msg pgs.Entity) string {
	return "::" + strings.Join(msg.Package().ProtoName().Split(), "::")
}

func (fns CCFuncs) quote(s interface {
	String() string
}) string {
	return strconv.Quote(s.String())
}

func (fns CCFuncs) err(ctx shared.RuleContext, reason ...interface{}) string {
	return fns.errIdxCause(ctx, "", "nil", reason...)
}

func (fns CCFuncs) errCause(ctx shared.RuleContext, cause string, reason ...interface{}) string {
	return fns.errIdxCause(ctx, "", cause, reason...)
}

func (fns CCFuncs) errIdx(ctx shared.RuleContext, idx string, reason ...interface{}) string {
	return fns.errIdxCause(ctx, idx, "nil", reason...)
}

func (fns CCFuncs) errIdxCause(ctx shared.RuleContext, idx, cause string, reason ...interface{}) string {
	f := ctx.Field
	errName := fmt.Sprintf("%sValidationError", f.Message().Name())

	output := []string{
		"{",
		`std::ostringstream msg("invalid ");`,
	}

	if ctx.OnKey {
		output = append(output, `msg << "key for ";`)
	}
	output = append(output,
		fmt.Sprintf(`msg << %q << "." << %s;`,
			errName, fns.lit(pgsgo.PGGUpperCamelCase(f.Name()))))

	if idx != "" {
		output = append(output, fmt.Sprintf(`msg << "[" << %s << "]";`, fns.lit(idx)))
	} else if ctx.Index != "" {
		output = append(output, fmt.Sprintf(`msg << "[" << %s << "]";`, fns.lit(ctx.Index)))
	}

	output = append(output, fmt.Sprintf(`msg << ": " << %s;`, fns.lit(fmt.Sprintf("%q", reason))))

	if cause != "nil" && cause != "" {
		output = append(output, fmt.Sprintf(`msg << " | caused by " << %s;`, cause))
	}

	output = append(output, "*err = msg.str();",
		"return false;",
		"}")
	return strings.Join(output, "\n")
}

func (fns CCFuncs) lookup(f pgs.Field, name string) string {
	return fmt.Sprintf(
		"_%s_%s_%s",
		pgsgo.PGGUpperCamelCase(f.Message().Name()),
		pgsgo.PGGUpperCamelCase(f.Name()),
		name,
	)
}

func (fns CCFuncs) lit(x interface{}) string {
	val := reflect.ValueOf(x)

	if val.Kind() == reflect.Interface {
		val = val.Elem()
	}

	if val.Kind() == reflect.Ptr {
		val = val.Elem()
	}

	switch val.Kind() {
	case reflect.String:
		return fmt.Sprintf("%q", x)
	case reflect.Uint8:
		return fmt.Sprintf("%d", x)
	case reflect.Slice:
		els := make([]string, val.Len())
		switch reflect.TypeOf(x).Elem().Kind() {
		case reflect.Uint8:
			for i, l := 0, val.Len(); i < l; i++ {
				els[i] = fmt.Sprintf("\\x%x", val.Index(i).Interface())
			}
			return fmt.Sprintf("\"%s\"", strings.Join(els, ""))
		default:
			panic(fmt.Sprintf("don't know how to format literals of type %v", val.Kind()))
		}
	case reflect.Float32:
		return fmt.Sprintf("%fF", x)
	default:
		return fmt.Sprint(x)
	}
}

func (fns CCFuncs) isBytes(f interface {
	ProtoType() pgs.ProtoType
}) bool {
	return f.ProtoType() == pgs.BytesT
}

func (fns CCFuncs) byteStr(x []byte) string {
	elms := make([]string, len(x))
	for i, b := range x {
		elms[i] = fmt.Sprintf(`\x%X`, b)
	}

	return fmt.Sprintf(`"%s"`, strings.Join(elms, ""))
}

func (fns CCFuncs) oneofTypeName(f pgs.Field) pgsgo.TypeName {
	return pgsgo.TypeName(fmt.Sprintf("%s::%sCase::k%s",
		fns.className(f.Message()),
		pgsgo.PGGUpperCamelCase(f.OneOf().Name()),
		strings.ReplaceAll(pgsgo.PGGUpperCamelCase(f.Name()).String(), "_", "")))
}

func (fns CCFuncs) inType(f pgs.Field, x interface{}) string {
	switch f.Type().ProtoType() {
	case pgs.BytesT:
		return "string"
	case pgs.MessageT:
		switch x.(type) {
		case []string:
			return "string"
		case []*duration.Duration:
			return "pgv::protobuf_wkt::Duration"
		default:
			return fns.className(f.Type().Element().Embed())
		}
	default:
		return fns.cType(f.Type())
	}
}

func (fns CCFuncs) cType(t pgs.FieldType) string {
	if t.IsEmbed() {
		return fns.className(t.Embed())
	}
	if t.IsRepeated() {
		if t.ProtoType() == pgs.MessageT {
			return fns.className(t.Element().Embed())
		}
		// Strip the leading []
		return fns.cTypeOfString(fns.Type(t.Field()).String()[2:])
	} else if t.IsMap() {
		if t.Element().IsEmbed() {
			return fns.className(t.Element().Embed())
		}

		return fns.cTypeOfString(fns.Type(t.Field()).Element().String())
	}

	return fns.cTypeOfString(fns.Type(t.Field()).String())
}

func (fns CCFuncs) cTypeOfString(s string) string {
	switch s {
	case "float32":
		return "float"
	case "float64":
		return "double"
	case "int32":
		return "int32_t"
	case "int64":
		return "int64_t"
	case "uint32":
		return "uint32_t"
	case "uint64":
		return "uint64_t"
	case "[]byte":
		return "string"
	default:
		return s
	}
}

func (fns CCFuncs) inKey(f pgs.Field, x interface{}) string {
	switch f.Type().ProtoType() {
	case pgs.BytesT:
		return fns.byteStr(x.([]byte))
	case pgs.MessageT:
		switch x := x.(type) {
		case *duration.Duration:
			return fns.durLit(x)
		default:
			return fns.lit(x)
		}
	case pgs.EnumT:
		return fmt.Sprintf("%s(%d)", fns.cType(f.Type()), x.(int32))
	default:
		return fns.lit(x)
	}
}

func (fns CCFuncs) durLit(dur *duration.Duration) string {
	return fmt.Sprintf(
		"pgv::protobuf::util::TimeUtil::SecondsToDuration(%d) + pgv::protobuf::util::TimeUtil::NanosecondsToDuration(%d)",
		dur.GetSeconds(), dur.GetNanos())
}

func (fns CCFuncs) durStr(dur *duration.Duration) string {
	d, _ := ptypes.Duration(dur)
	return d.String()
}

func (fns CCFuncs) durGt(a, b *duration.Duration) bool {
	ad, _ := ptypes.Duration(a)
	bd, _ := ptypes.Duration(b)

	return ad > bd
}

func (fns CCFuncs) tsLit(ts *timestamp.Timestamp) string {
	return fmt.Sprintf(
		"time.Unix(%d, %d)",
		ts.GetSeconds(), ts.GetNanos(),
	)
}

func (fns CCFuncs) tsGt(a, b *timestamp.Timestamp) bool {
	at, _ := ptypes.Timestamp(a)
	bt, _ := ptypes.Timestamp(b)

	return !bt.Before(at)
}

func (fns CCFuncs) tsStr(ts *timestamp.Timestamp) string {
	t, _ := ptypes.Timestamp(ts)
	return t.String()
}

func (fns CCFuncs) unwrap(ctx shared.RuleContext, name string) (shared.RuleContext, error) {
	ctx, err := ctx.Unwrap("wrapper")
	if err != nil {
		return ctx, err
	}

	ctx.AccessorOverride = fmt.Sprintf("%s.%s()", name,
		ctx.Field.Type().Embed().Fields()[0].Name())

	return ctx, nil
}

func (fns CCFuncs) failUnimplemented(message string) string {
	if len(message) == 0 {
		return "throw pgv::UnimplementedException();"
	}

	return fmt.Sprintf(`throw pgv::UnimplementedException(%q);`, message)
}

func (fns CCFuncs) staticVarName(msg pgs.Message) string {
	return "validator_" + strings.Replace(fns.className(msg), ":", "_", -1)
}

func (fns CCFuncs) output(file pgs.File, ext string) string {
	return pgs.FilePath(file.Name().String()).SetExt(".pb" + ext).String()
}

func (fns CCFuncs) Type(f pgs.Field) pgsgo.TypeName {
	typ := fns.Context.Type(f)

	if f.Type().IsEnum() {
		parts := strings.Split(typ.String(), ".")
		typ = pgsgo.TypeName(parts[len(parts)-1])
	}

	return typ
}
