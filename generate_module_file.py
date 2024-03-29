import sys
import regex
import json

re = regex

files = {}

for filename in sys.argv:
    with open(filename, 'r') as file:
        files[filename] = file.read()

exprs = {}
exprs["s"] = "\s*"
exprs["reference"] = "(\w+)\s*,\s*(\w+)\s*,\s*(\w+)"
exprs["type_reference"] = "TYPE\s*\(\s*{reference}\s*\)".format_map(exprs)
exprs["type_reference_with_generics"] = "TYPE\s*\(\s*{reference}\s*(?:,\s*{type_reference}\s*)*\)".format_map(exprs)

exprs["no_newline_space"] = "[ \t]"
exprs["doc_comment"] = "(?:{no_newline_space}*//(.*)\n)*\s*".format_map(exprs)
exprs["type_declaration"] = "{doc_comment}DECLARE_TYPE\s*\(\s*{reference}\s*\)\s*;".format_map(exprs)
exprs["generic_type"] = "GENERIC_TYPE\s*\(\s*(\w+)\s*\)".format_map(exprs)
exprs["generic_type_declaration"] = "{doc_comment}DECLARE_GENERIC_TYPE{s}\({s}(\w+){s},{s}(\w+),\s(\w+)(?:{s},{s}{generic_type})*{s}\){s};".format_map(exprs)
exprs["type_property"] = "{doc_comment}DECLARE_PROPERTY\s*\(\s*{type_reference}\s*,\s*(\w+)\s*,\s*{type_reference}\s*\)\s*;".format_map(exprs)
exprs["type_property_getter"] = "{doc_comment}DECLARE_PROPERTY_GETTER\s*\(\s*{type_reference}\s*,\s*(\w+)\s*,\s*{type_reference}\s*\)\s*;".format_map(exprs)
exprs["type_property_setter"] = "{doc_comment}DECLARE_PROPERTY_SETTER\s*\(\s*{type_reference}\s*,\s*(\w+)\s*,\s*{type_reference}\s*\)\s*;".format_map(exprs)
exprs["type_alias"] = "DECLARE_TYPE_ALIAS\s*\(\s*{type_reference}\s*,\s*{type_reference_with_generics}\s*\)\s*;".format_map(exprs)
exprs["resource_declaration"] = "DECLARE_RESOURCE\s*\((Event|SceneComponent|ViewportComponent|EntitySpawnList)\s*,\s*{type_reference}\s*\)".format_map(exprs)
# exprs["scene_component"] = "SCENE_COMPONENT\s*\(\s*{type_reference}\s*\)".format_map(exprs)
# exprs["event"] = "EVENT\s*\(\s*{type_reference}\s*\)".format_map(exprs)
# exprs["viewport_component"] = "VIEWPORT_COMPONENT\s*\(\s*{type_reference}\s*\)".format_map(exprs)

exprs["generic"] = "GENERIC\s*\(\s*(\w+)\s*\)".format_map(exprs)
exprs["generics"] = "(?:,\s*{generic}\s*)*".format_map(exprs)
exprs["parameter"] = "(?:PARAMETER|GENERIC_PARAMETER)\s*\(\s*(\w+)\s*,\s*({type_reference}|(\w+))\s*\)".format_map(exprs)
exprs["function_reference"] = "FUNCTION\s*\(\s*{reference}\s*\)".format_map(exprs)
exprs["result"] = "RESULT\s*\(?\s*{type_reference}\s*\)".format_map(exprs)

exprs["parameters_and_result"] = "(?:\s*,\s*{parameter})*(?:\s*,\s*{result})?".format_map(exprs)
exprs["free_function"] = "DECLARE_FUNCTION\s*\(\s*{function_reference}".format_map(exprs)
exprs["type_function"] = "DECLARE_(TYPE_FUNCTION|MEMBER_FUNCTION|MUTABLE_MEMBER_FUNCTION)\s*\(\s*{type_reference}\s*,\s*(\w+)".format_map(exprs)
exprs["function"] = "(?:{free_function}|{type_function})\s*{generics}\s*{parameters_and_result}\s*\)\s*;".format_map(exprs)

modules = {}

def create_reference(owner, project, definition):
    return {
        "owner": owner,
        "project": project,
        "definition": definition,
    }

def create_reference_with_generics(owner, project, definition, generics):
    return {
        "owner": owner,
        "project": project,
        "definition": definition,
        "generics": generics,
    }


def create_type_decl(name, description):
    return {
        "Struct": {
            "name": name,
            "description": description,
            "generics": [],
            "properties": [],
            "functions": [],
        }
    }

def create_type_alias(name, type_):
    return {
        "TypeAlias": {
            "name": name,
            "type": type_,
        }
    }

def create_function_decl(name):
    return {
        "Function": {
            "name": name,
            "inputs": [],
        }
    }

def create_param(name, owner, project, t):
    return {
        "name": name,
        "type": create_reference(owner, project, t),
    }

def create_generic_param(name, g):
    return {
        "name": name,
        "type": g,
    }

def create_property(name, owner, project, t):
    return {
        "name": name,
        "type": create_reference(owner, project, t),
    }

def get_type(module, name):
    for d in modules[module]["declarations"]:
        if "Struct" in d and d["Struct"]["name"] == name:
            return d["Struct"]
        elif "TypeAlias" in d and d["TypeAlias"]["name"] == name:
            return d["TypeAlias"]
    return None

def get_property(type_, name):
    for p in type_["properties"]:
        if p["name"] == name:
            return p
    return None


for f in files:
    content = files[f]

    for m in re.finditer(exprs["type_declaration"], content, flags=re.MULTILINE):
        desc = " ".join(map(lambda s: s.strip(), m.captures(1)))
        module = "{}/{}".format(m[2], m[3])
        if not module in modules:
            modules[module] = {
                "module": module,
                "declarations": []
            }
        modules[module]["declarations"].append(create_type_decl(m[4], desc))

    for m in re.finditer(exprs["generic_type_declaration"], content, flags=re.MULTILINE):
        desc = " ".join(map(lambda s: s.strip(), m.captures(1)))
        module = "{}/{}".format(m[2], m[3])
        if not module in modules:
            modules[module] = {
                "module": module,
                "declarations": []
            }
        type_ = create_type_decl(m[4], desc)
        type_["Struct"]["generics"] = m.captures(5)
        modules[module]["declarations"].append(type_)

    for m in re.finditer(exprs["type_alias"], content):
        module = "{}/{}".format(m[1], m[2])
        if not module in modules:
            modules[module] = {
                "module": module,
                "declarations": []
            }
        generics = []
        for i in range(0, len(m.captures(7))):
            generics.append(create_reference(m.captures(7)[i], m.captures(8)[i], m.captures(9)[i]))
            
        modules[module]["declarations"].append(create_type_alias(m[3], create_reference_with_generics(m[4], m[5], m[6], generics)))

    for m in re.finditer(exprs["resource_declaration"], content):
        module = "{}/{}".format(m[2], m[3])
        type_ = get_type("{}/{}".format(m[2], m[3]), m[4])
        type_["resource"] = m[1]

    for m in re.finditer(exprs["type_property"], content, flags=re.MULTILINE):
        type_ = get_type("{}/{}".format(m[2], m[3]), m[4])
        prop = create_property(m[5], m[6], m[7], m[8])
        prop["description"] = " ".join(map(lambda s: s.strip(), m.captures(1)))
        prop["get"] = True
        prop["set"] = True
        type_["properties"].append(prop)

    for m in re.finditer(exprs["type_property_getter"], content, flags=re.MULTILINE):
        type_ = get_type("{}/{}".format(m[2], m[3]), m[4])
        prop = get_property(type_, m[5])
        if prop == None:
            prop = create_property(m[5], m[6], m[7], m[8])
            prop["description"] = " ".join(map(lambda s: s.strip(), m.captures(1)))
            prop["get"] = True
            type_["properties"].append(prop)
        else:
            prop["get"] = True

    for m in re.finditer(exprs["type_property_setter"], content, flags=re.MULTILINE):
        type_ = get_type("{}/{}".format(m[2], m[3]), m[4])
        prop["description"] = " ".join(map(lambda s: s.strip(), m.captures(1)))
        prop = get_property(type_, m[5])
        if prop == None:
            prop = create_property(m[5], m[6], m[7], m[8])
            prop["description"] = " ".join(map(lambda s: s.strip(), m.captures(1)))
            prop["set"] = True
            type_["properties"].append(prop)
        else:
            prop["set"] = True

    
    for m in re.finditer(exprs["function"], content):
        if m[1]: # free function
            f = create_function_decl(m[3])
            module = "{}/{}".format(m[1], m[2])
            if not module in modules:
                modules[module] = {
                    "module": module,
                    "declarations": []
                }
            modules[module]["declarations"].append(f)
        else:
            f = create_function_decl(m[8])
            if m[4] == "TYPE_FUNCTION":
                pass
            elif m[4] == "MEMBER_FUNCTION":
                f["Function"]["inputs"].append(create_param("self", m[5], m[6], m[7]))
            elif m[4] == "MUTABLE_MEMBER_FUNCTION":
                f["Function"]["inputs"].append(create_param("self", m[5], m[6], m[7]))
            t = get_type("{}/{}".format(m[5], m[6]), m[7])
            t["functions"].append(f["Function"])

        f["Function"]["generics"] = m.captures(9)

        input_base = 10
        for i in range(0, len(m.captures(input_base))):
            p = re.match(exprs["type_reference"], m.captures(input_base + 1)[i])
            if p:
                param = create_param(m.captures(input_base)[i], p[1], p[2], p[3])
            else:
                param = create_generic_param(m.captures(input_base)[i], m.captures(input_base + 1)[i])
            f["Function"]["inputs"].append(param)
        
        output_base = input_base + 6
        if m[output_base] != None:
            f["Function"]["output"] = create_reference(m[output_base + 0], m[output_base + 1], m[output_base + 2])

for m in modules:
    with open("{}.json".format(m.replace("/", "--")), "w") as write:
        json.dump(modules[m], write)
