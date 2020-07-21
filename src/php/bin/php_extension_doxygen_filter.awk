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
    delete methods; # methods[method][doc|args|static]
    delete constants; # constants[i][name|doc]
    constantsCount = 0;

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
    className = gensub(classLineRegex, "\\1", "g");
}

# end of class document
inDocComment==1 && /\*\// && classDocComment == "" {
    classDocComment = docComment;
    docComment = "";
}

# param line
inDocComment==1 && $0 ~ paramLineRegex {
    arg = gensub(paramLineRegex, "\\1", "g");
    args[argsCount]=arg;
    argsCount++;
}

# '*/' document comment end
inDocComment==1 && /\*\// {
    inDocComment = 0;
}

# PHP_METHOD
$0 ~ phpMethodLineRegex {
    class = gensub(phpMethodLineRegex, "\\1", "g");
    if (class != className) {
        error = "ERROR: Missing or mismatch class names, in class comment block: " \
          className ", in PHP_METHOD: " class;
        exit;
    };

    method = gensub(phpMethodLineRegex, "\\2", "g");
    methods[method]["doc"] = docComment;
    for (i in args) {
        methods[method]["args"][i] = args[i];
    }
    docComment = "";
}

# PHP_ME(class, function,...
$0 ~ phpMeLineRegex {
    inPhpMe = 1;

    class = gensub(phpMeLineRegex, "\\1", "g");
    if (class != className) {
        error = "ERROR: Missing or mismatch class names, in class comment block: " \
          className ", in PHP_ME: " class;
        exit;
    };
    method = gensub(phpMeLineRegex, "\\2", "g");
}

# ZEND_ACC_STATIC
inPhpMe && /ZEND_ACC_STATIC/ { 
    methods[method]["static"] = 1;
}

# closing bracet of PHP_ME(...)
iinPhpMe && /\)$/ {
    inPhpMe = 0;
}

# REGISTER_LONG_CONSTANT(constant, ...
$0 ~ phpConstantLineRegs {
    inPhpConstant = 1;
    constant = gensub(phpConstantLineRegs, "\\1", "g");
    constants[constantsCount]["name"] = constant;
    constants[constantsCount]["doc"] = docComment;
    constantsCount++;
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
        for (m in methods) {
            if (hideMethods[className"::"m]) continue;

            print methods[m]["doc"];
            printf "public"
            if (methods[m]["static"]) printf " static"
            printf " function " m "("
            if (isarray(methods[m]["args"])) {
                printf methods[m]["args"][0];
                for (i = 1; i < length(methods[m]["args"]); i++) {
                    printf ", " methods[m]["args"][i];
                }
            }
            print ") {}";
        }
        print "\n}";
    }

    for (i in constants) {
        print constants[i]["doc"];
        print "const " constants[i]["name"] " = 0;";
    }

    print "\n}";
}
