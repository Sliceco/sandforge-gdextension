#!/usr/bin/env python
import os
import sys

from methods import print_error


libname = "SandForge"
projectdir = "project"

localEnv = Environment(tools=["default"], PLATFORM="")

# Build profiles can be used to decrease compile times.
# You can either specify "disabled_classes", OR
# explicitly specify "enabled_classes" which disables all other classes.
# Modify the example file as needed and uncomment the line below or
# manually specify the build_profile parameter when running SCons.

# localEnv["build_profile"] = "build_profile.json"

customs = ["custom.py"]
customs = [os.path.abspath(path) for path in customs]

opts = Variables(customs, ARGUMENTS)
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()

if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error("""godot-cpp is not available within this folder, as Git submodules haven't been initialized.
Run the following command to download godot-cpp:

    git submodule update --init --recursive""")
    sys.exit(1)

env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

env.Append(CPPPATH=["src/"])
sources = Glob("src/*.cpp")

if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")


def embed_default_materials(target, source, env):
    """Validate the default materials JSON and compile it in as a C string."""
    import json

    with open(str(source[0]), "rb") as f:
        raw = f.read()
    try:
        materials = json.loads(raw)
    except json.JSONDecodeError as e:
        print_error("{}: invalid JSON: {}".format(source[0], e))
        return 1
    ids = set()
    for key in materials:
        if not key.isdigit() or not 1 <= int(key) <= 255:
            print_error("{}: material key '{}' must be an id in 1..255".format(source[0], key))
            return 1
        ids.add(int(key))
    for key, material in materials.items():
        referenced = [material.get("decay_into", 0), material.get("break_into", 0)]
        for reaction in material.get("reactions", []):
            referenced += [reaction.get("other", 0), reaction.get("into", 0), reaction.get("other_into", 0)]
        for mat_id in referenced:
            if mat_id != 0 and mat_id not in ids:
                print_error("{}: material {} references unknown material {}".format(source[0], key, mat_id))
                return 1

    body = ",".join(str(b) for b in raw + b"\0")
    with open(str(target[0]), "w") as f:
        f.write('#include "default_materials.h"\n\n')
        f.write("const char DEFAULT_MATERIALS_JSON[] = {{ {} }};\n".format(body))
    return 0


sources.append(env.Command(
    "src/gen/default_materials.gen.cpp",
    "data/default_materials.json",
    Action(embed_default_materials, "Embedding default materials ..."),
))

# .dev doesn't inhibit compatibility, so we don't need to key it.
# .universal just means "compatible with all relevant arches" so we don't need to key it.
suffix = env['suffix'].replace(".dev", "").replace(".universal", "")

lib_filename = "{}{}{}{}".format(env.subst('$SHLIBPREFIX'), libname, suffix, env.subst('$SHLIBSUFFIX'))

library = env.SharedLibrary(
    "bin/{}/{}".format(env['platform'], lib_filename),
    source=sources,
)

copy = env.Install("{}/bin/{}/".format(projectdir, env["platform"]), library)

test_program = env.Program(
    "build/tests/world_grid_tests",
    source=[
        "tests/world_grid_tests.cpp",
        "src/sand_chunk.cpp",
        "src/world_grid.cpp",
    ],
)
test_result = env.Command(
    "build/tests/world_grid_tests.passed",
    test_program,
    "$SOURCE && touch $TARGET",
)
env.Alias("test", test_result)

default_args = [library, copy]
Default(*default_args)
