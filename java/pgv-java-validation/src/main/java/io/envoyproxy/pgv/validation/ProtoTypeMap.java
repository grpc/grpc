package io.envoyproxy.pgv.validation;

import com.google.common.base.*;
import com.google.common.collect.ImmutableMap;
import com.google.protobuf.Any;
import com.google.protobuf.DescriptorProtos;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.Message;

import java.util.Collection;

/**
 * {@code ProtoTypeMap} maintains a dictionary for looking up Java type names when given proto types.
 */
public final class ProtoTypeMap {

    private final ImmutableMap<String, String> types;

    private ProtoTypeMap(ImmutableMap<String, String> types) {
        Preconditions.checkNotNull(types, "types");

        this.types = types;
    }

    /**
     * Returns an instance of {@link ProtoTypeMap} based on the given FileDescriptorProto instances.
     *
     * @param fileDescriptorProtos the full collection of files descriptors from the code generator request
     */
    public static ProtoTypeMap of(Collection<DescriptorProtos.FileDescriptorProto> fileDescriptorProtos) {
        Preconditions.checkNotNull(fileDescriptorProtos, "fileDescriptorProtos");
        Preconditions.checkArgument(!fileDescriptorProtos.isEmpty(), "fileDescriptorProtos.isEmpty()");

        final ImmutableMap.Builder<String, String> types = ImmutableMap.builder();

        for (final DescriptorProtos.FileDescriptorProto fileDescriptor : fileDescriptorProtos) {
            final DescriptorProtos.FileOptions fileOptions = fileDescriptor.getOptions();

            final String protoPackage = fileDescriptor.hasPackage() ? "." + fileDescriptor.getPackage() : "";
            final String javaPackage = Strings.emptyToNull(
                    fileOptions.hasJavaPackage() ?
                            fileOptions.getJavaPackage() :
                            fileDescriptor.getPackage());
            final String enclosingClassName =
                    fileOptions.getJavaMultipleFiles() ?
                            null :
                            getJavaOuterClassname(fileDescriptor, fileOptions);


            // Identify top-level enums
            fileDescriptor.getEnumTypeList().forEach(
                    e -> types.put(
                            protoPackage + "." + e.getName(),
                            toJavaTypeName(e.getName(), enclosingClassName, javaPackage)));

            // Identify top-level messages, and nested types
            fileDescriptor.getMessageTypeList().forEach(
                    m -> recursivelyAddTypes(types, m, protoPackage, enclosingClassName, javaPackage)
            );
        }

        return new ProtoTypeMap(types.build());
    }

    private static void recursivelyAddTypes(ImmutableMap.Builder<String, String> types, DescriptorProtos.DescriptorProto m, String protoPackage, String enclosingClassName, String javaPackage) {
        // Identify current type
        types.put(
                protoPackage + "." + m.getName(),
                toJavaTypeName(m.getName(), enclosingClassName, javaPackage));

        // Identify any nested Enums
        m.getEnumTypeList().forEach(
                e -> types.put(
                        protoPackage + "." + e.getName(),
                        toJavaTypeName(e.getName(),
                                enclosingClassName,
                                javaPackage)));

        // Recursively identify any nested types
        m.getNestedTypeList().forEach(
                n -> recursivelyAddTypes(
                        types,
                        n,
                        protoPackage + "." + m.getName(),
                        enclosingClassName + "$" + m.getName(),
                        javaPackage));
    }

    /**
     * Returns the full Java type name for the given proto type.
     *
     * @param protoTypeName the proto type to be converted to a Java type
     */
    public String toJavaTypeName(String protoTypeName) {
        Preconditions.checkNotNull(protoTypeName, "protoTypeName");
        return types.get(protoTypeName);
    }

    /**
     * Returns the full Java type name based on the given protobuf type parameters.
     *
     * @param className          the protobuf type name
     * @param enclosingClassName the optional enclosing class for the given type
     * @param javaPackage        the proto file's configured java package name
     */
    private static String toJavaTypeName(
            String className,
            String enclosingClassName,
            String javaPackage) {

        Preconditions.checkNotNull(className, "className");

        Joiner dotJoiner = Joiner.on('.').skipNulls();
        Joiner dollarJoiner = Joiner.on('$').skipNulls();
        return dotJoiner.join(javaPackage, dollarJoiner.join(enclosingClassName, className));
    }

    private static String getJavaOuterClassname(
            DescriptorProtos.FileDescriptorProto fileDescriptor,
            DescriptorProtos.FileOptions fileOptions) {

        if (fileOptions.hasJavaOuterClassname()) {
            return fileOptions.getJavaOuterClassname();
        }

        // If the outer class name is not explicitly defined, then we take the proto filename, strip its extension,
        // and convert it from snake case to camel case.
        String filename = fileDescriptor.getName().substring(0, fileDescriptor.getName().length() - ".proto".length());

        // Protos in subdirectories without java_outer_classname have their path prepended to the filename. Remove
        // if present.
        if (filename.contains("/")) {
            filename = filename.substring(filename.lastIndexOf('/') + 1);
        }

        filename = makeInvalidCharactersUnderscores(filename);
        filename = CaseFormat.LOWER_UNDERSCORE.to(CaseFormat.UPPER_CAMEL, filename);
        filename = upcaseAfterNumber(filename);
        filename = appendOuterClassSuffix(filename, fileDescriptor);
        return filename;
    }

    /**
     * In the event of a name conflict between the outer and inner type names, protoc adds an OuterClass suffix to the
     * outer type's name.
     */
    private static String appendOuterClassSuffix(final String enclosingClassName, DescriptorProtos.FileDescriptorProto fd) {
        if (fd.getEnumTypeList().stream().anyMatch(enumProto -> enumProto.getName().equals(enclosingClassName)) ||
                fd.getMessageTypeList().stream().anyMatch(messageProto -> messageProto.getName().equals(enclosingClassName)) ||
                fd.getServiceList().stream().anyMatch(serviceProto -> serviceProto.getName().equals(enclosingClassName))) {
            return enclosingClassName + "OuterClass";
        } else {
            return enclosingClassName;
        }
    }

    /**
     * Replace invalid proto identifier characters with an underscore, so they will be dropped and camel cases below.
     * https://developers.google.com/protocol-buffers/docs/reference/proto3-spec
     */
    private static String makeInvalidCharactersUnderscores(String filename) {
        char[] filechars = filename.toCharArray();
        for (int i = 0; i < filechars.length; i++) {
            char c = filechars[i];
            if (!CharMatcher.inRange('0', '9').or(CharMatcher.inRange('A', 'Z')).or(CharMatcher.inRange('a', 'z')).matches(c)) {
                filechars[i] = '_';
            }
        }
        return new String(filechars);
    }

    /**
     * Upper case characters after numbers, like {@code Weyland9Yutani}.
     */
    private static String upcaseAfterNumber(String filename) {
        char[] filechars = filename.toCharArray();
        for (int i = 1; i < filechars.length; i++) {
            if (CharMatcher.inRange('0', '9').matches(filechars[i - 1])) {
                filechars[i] = Character.toUpperCase(filechars[i]);
            }
        }
        return new String(filechars);
    }

    @SuppressWarnings("unchecked")
    public Message unpackAny(Any any) throws ClassNotFoundException, InvalidProtocolBufferException {
        String typeUrl = any.getTypeUrl();
        if (typeUrl.startsWith("type.googleapis.com/")){
            String protoTypeName = any.getTypeUrl().substring(20);

            String className = toJavaTypeName("." + protoTypeName);
            if (className == null) {
                throw new IllegalArgumentException(protoTypeName + " does not map to a Java type name");
            }

            Class<Message> clazz = (Class<Message>) Class.forName(className);
            return any.unpack(clazz);
        } else {
            throw new IllegalArgumentException("Any typeUrl does not start with type.googleapis.com - " + typeUrl);
        }
    }
}
