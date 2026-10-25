"""Create or refresh /Game/Tharrn/Maps/L_Slice_DeepCaverns.

The map holds only the district builder, a nav-mesh bounds volume sized from the layout and a player start;
everything else is built from Content/TharrnData/district_slice.json at load. Safe to re-run.

Run headless:
    UnrealEditor-Cmd.exe <path>/Tharrn.uproject -run=pythonscript -script="<path>/Tools/create_slice_map.py" -unattended
"""
import json
import os

import unreal

MAP = "/Game/Tharrn/Maps/L_Slice_DeepCaverns"
LAYOUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content", "TharrnData", "district_slice.json")


def main():
    with open(LAYOUT, encoding="utf-8") as handle:
        layout = json.load(handle)

    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        levels.load_level(MAP)
    else:
        levels.new_level(MAP)

    builder_class = unreal.load_class(None, "/Script/Tharrn.TharrnDistrictBuilder")
    for actor in actors.get_all_level_actors():
        if isinstance(actor, (unreal.NavMeshBoundsVolume, unreal.PlayerStart)) or actor.get_class() == builder_class:
            actors.destroy_actor(actor)

    builder = actors.spawn_actor_from_class(builder_class, unreal.Vector(0, 0, 0))
    builder.set_actor_label("DistrictBuilder")

    rooms = layout["Rooms"]
    min_x = min(r["Min"]["X"] for r in rooms) - 200
    max_x = max(r["Max"]["X"] for r in rooms) + 200
    min_y = min(r["Min"]["Y"] for r in rooms) - 200
    max_y = max(r["Max"]["Y"] for r in rooms) + 200
    top = max(r.get("FloorZ", 0) + r.get("Height", 800) for r in rooms) + 100
    center = unreal.Vector((min_x + max_x) / 2, (min_y + max_y) / 2, top / 2 - 100)
    nav = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, center)
    # The default bounds brush is 200 units on a side.
    nav.set_actor_scale3d(unreal.Vector((max_x - min_x) / 200.0, (max_y - min_y) / 200.0, (top + 200) / 200.0))
    nav.set_actor_label("SliceNavBounds")

    arrival = layout["ArrivalPoint"]
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(arrival["X"], arrival["Y"], arrival["Z"]),
                                          unreal.Rotator(roll=0.0, pitch=0.0, yaw=float(layout.get("ArrivalYaw", 0))))
    start.set_actor_label("ArrivalStart")

    # The slice runs its own game mode; other levels keep the project default (the Third Person template's).
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Script/Tharrn.TharrnGameMode"))

    levels.save_current_level()
    unreal.log("Tharrn: slice map saved with builder, nav bounds {} and arrival start".format(nav.get_actor_scale3d()))


main()
