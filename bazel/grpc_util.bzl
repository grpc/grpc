# Follows convention set in objectivec_helpers.cc in the protobuf ObjC compiler.
_upper_segments_list = ["url", "http", "https"]

def strip_extension(str):
    return str.rpartition(".")[0]

def capitalize(word):
    if word in _upper_segments_list:
        return word.upper()
    else:
        return word.capitalize()

def lower_underscore_to_upper_camel(str):
    str = strip_extension(str)
    camel_case_str = ""
    word = ""
    for c in str.elems():  # NB: assumes ASCII!
        if c.isalpha():
            word += c.lower()
        else:
            # Last word is finished.
            if len(word):
                camel_case_str += capitalize(word)
                word = ""
            if c.isdigit():
                camel_case_str += c

            # Otherwise, drop the character. See UnderscoresToCamelCase in:
            # third_party/protobuf/src/google/protobuf/compiler/objectivec/objectivec_helpers.cc

    if len(word):
        camel_case_str += capitalize(word)
    return camel_case_str

def file_to_upper_camel(src):
    elements = src.rpartition("/")
    upper_camel = lower_underscore_to_upper_camel(elements[-1])
    return "".join(list(elements[:-1]) + [upper_camel])

def file_with_extension(src, ext):
    elements = src.rpartition("/")
    return "".join(list(elements[:-1]) + [elements[-1], "." + ext])

def to_upper_camel_with_extension(src, ext):
    src = file_to_upper_camel(src)
    return file_with_extension(src, ext)
