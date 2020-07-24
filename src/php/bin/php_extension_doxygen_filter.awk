# Copyright 2020 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

BEGIN {
    namespace = "Grpc";
    className = "";
    classDocComment = "";
    delete methodDocComments;
    delete methodIsStatic;
    delete methodArgs;
    delete methodNumArgs;
    delete constantDocComments;

    #  * class className
    classLineRegex = "^ \\* class (\\S+)$";
    # @param type name [= default]
    paramLineRegex = "^.*@param\\s+\\S+\\s+(\\$\\S+(\\s+=\\s+\\S+)?)\\s+.*$";
    # PHP_METHOD(class, function)
    phpMethodLineRegex = "^PHP_METHOD\\((\\S+),\\s*(\\S+)\\).*$";

    # PHP_ME(class, function, arginfo, flags)
    phpMeLineRegex = "^\\s*PHP_ME\\((\\S+),\\s*(\\S+),.*$";

    # REGISTER_LONG_CONSTANT("namespace\\constant", grpcConstant, ..)
    phpConstantLineRegs = "^\\s*REGISTER_LONG_CONSTANT\\(\"Grpc\\\\\\\\(\\S+)\",.*$";

    error = "";

    # extension testing methods
    hideMethods["Channel::getChannelInfo"] = 1;
    hideMethods["Channel::cleanPersistentList"] = 1;
    hideMethods["Channel::getPersistentList"] = 1;

}

# '/**' document comment start
/^\s*\/\*\*/ {
    inDocComment = 1;
    docComment = "";
    delete args;
    argsCount = 0;
}

# collect document comment
inDocComment==1 {
    docComment = docComment"\n"$0
}

# class document, must match ' * class <clasName>'
inDocComment==1 && $0 ~ classLineRegex {
    match($0, classLineRegex, arr);
    className = arr[1];
}

# end of class document
inDocComment==1 && /\*\// && classDocComment == "" {
    classDocComment = docComment;
    docComment = "";
}

# param line
inDocComment==1 && $0 ~ paramLineRegex {
    match($0, paramLineRegex, arr);
    arg = arr[1];
    args[argsCount]=arg;
    argsCount++;
}

# '*/' document comment end
inDocComment==1 && /\*\// {
    inDocComment = 0;
}

# PHP_METHOD
$0 ~ phpMethodLineRegex {
    match($0, phpMethodLineRegex, arr);
    class = arr[1];
    if (class != className) {
        error = "ERROR: Missing or mismatch class names, in class comment block: " \
          className ", in PHP_METHOD: " class;
        exit;
    };

    match($0, phpMethodLineRegex, arr);
    method = arr[2];
    methodDocComments[method] = docComment;
    for (i in args) {
        methodArgs[method, i] = args[i];
        methodNumArgs[method] = i"";
    }
    docComment = "";
}

# PHP_ME(class, function,...
$0 ~ phpMeLineRegex {
    inPhpMe = 1;

    match($0, phpMeLineRegex, arr);
    class = arr[1];
    if (class != className) {
        error = "ERROR: Missing or mismatch class names, in class comment block: " \
          className ", in PHP_ME: " class;
        exit;
    };
    match($0, phpMeLineRegex, arr);
    method = arr[2];
}

# ZEND_ACC_STATIC
inPhpMe && /ZEND_ACC_STATIC/ { 
    methodIsStatic[method] = 1;
}

# closing bracet of PHP_ME(...)
iinPhpMe && /\)$/ {
    inPhpMe = 0;
}

# REGISTER_LONG_CONSTANT(constant, ...
$0 ~ phpConstantLineRegs {
    inPhpConstant = 1;
    match($0, phpConstantLineRegs, arr);
    constant = arr[1];
    constantDocComments[constant] = docComment;
}

# closing bracet of PHP_ME(...)
inPhpConstant && /\)\s*;\s*$/ {
    inPhpConstant = 0;
    docComment = "";
}

END {
    if (error) {
        print error > "/dev/stderr";
        exit 1;
    }

    print "<?php\n"
    print "namespace " namespace "{";

    if (className != "") {
        print classDocComment
        print "class " className " {";
        for (m in methodDocComments) {
            if (hideMethods[className"::"m]) continue;

            print methodDocComments[m];
            printf "public"
            if (methodIsStatic[m]) printf " static"
            printf " function " m "("
            for (combined in methodArgs) {
              split(combined, sep, SUBSEP);
              if (sep[1] != m) continue;
              printf methodArgs[combined];
              if (sep[2] != methodNumArgs[m]) printf ", ";
            }
            print ") {}";
        }
        print "\n}";
    }

    for (i in constantDocComments) {
        print constantDocComments[i];
        print "const " i " = 0;";
    }

    print "\n}";
}
