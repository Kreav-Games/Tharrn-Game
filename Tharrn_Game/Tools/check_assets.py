"""Load every asset in the Third Person template folders and the Tharrn slice, and report anything broken.

A Blueprint counts as broken when it fails to load, has no generated class, or its default object no longer
derives from any native class beyond Object (the symptom of a missing or renamed C++ parent). Maps are loaded as packages; their actor load warnings
appear in the log, which run_checks.ps1 scans as well.

Run headless:
    UnrealEditor-Cmd.exe <path>/Tharrn_Game.uproject -run=pythonscript -script="<path>/Tools/check_assets.py" -unattended
"""
import unreal

ROOTS = ["/Game/ThirdPerson", "/Game/Variant_Combat", "/Game/Variant_Platforming", "/Game/Variant_SideScrolling",
         "/Game/Characters", "/Game/Input", "/Game/LevelPrototyping", "/Game/Tharrn"]


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    checked = 0
    blueprints = 0
    maps = 0
    problems = []
    for root in ROOTS:
        for data in registry.get_assets_by_path(root, recursive=True):
            path = "{}.{}".format(data.package_name, data.asset_name)
            asset = unreal.load_asset(path)
            checked += 1
            if asset is None:
                problems.append("failed to load " + path)
                continue
            if isinstance(asset, unreal.World):
                maps += 1
            if isinstance(asset, unreal.Blueprint):
                blueprints += 1
                generated = asset.generated_class()
                default = unreal.get_default_object(generated) if generated else None
                # The default object's Python type is its nearest native class; a lost C++ parent shows as Object.
                native = type(default).__name__ if default is not None else "None"
                if generated is None or default is None or native == "Object":
                    problems.append("parent class missing: {} (native {})".format(path, native))
                else:
                    unreal.log("THARRN_ASSET_CHECK bp {} -> {}".format(path, native))
    for problem in problems:
        unreal.log_error("THARRN_ASSET_CHECK problem: " + problem)
    unreal.log("THARRN_ASSET_CHECK summary checked={} blueprints={} maps={} problems={}".format(checked, blueprints, maps, len(problems)))


main()
