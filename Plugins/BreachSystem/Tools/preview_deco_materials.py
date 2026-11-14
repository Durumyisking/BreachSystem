"""Disposable offscreen material preview. No map or asset is saved."""
import json
import time
from pathlib import Path

import unreal

out = Path(unreal.Paths.project_saved_dir()).resolve() / "Automation/DecoMaterials"
out.mkdir(parents=True, exist_ok=True)
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
unreal.SystemLibrary.execute_console_command(world, "r.Streaming.FullyLoadUsedTextures 1")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
plane = unreal.load_asset("/Engine/BasicShapes/Plane")
plane_rotation = unreal.MathLibrary.make_rot_from_xz(unreal.Vector(1, 0, 0), unreal.Vector(0, -1, 0))
for index, kind in enumerate(("Concrete", "Wood")):
    material = unreal.load_asset(f"/BreachSystem/Decoration/Materials/M_Deco_{kind}")
    if not material:
        raise RuntimeError(f"Missing saved {kind} material")
    errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if errors:
        raise RuntimeError(str(errors))
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(-65 + index * 130, 0, 0), plane_rotation)
    actor.set_actor_location(unreal.Vector(-65 + index * 130, 0, 0), False, True)
    actor.set_actor_rotation(plane_rotation, False)
    if actor.get_actor_up_vector().y >= 0:
        raise RuntimeError("Preview plane must face the camera on the -Y side")
    component = actor.static_mesh_component
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_static_mesh(plane)
    component.set_material(0, material)
    actor.set_actor_scale3d(unreal.Vector(1.15, 1.15, 1))

for location, intensity in ((unreal.Vector(-150, -220, 220), 5.0), (unreal.Vector(160, -150, -60), 2.5)):
    rotation = unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(0, 0, 0))
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, location, rotation)
    light.set_actor_rotation(rotation, False)
    light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.light_component.set_editor_property("intensity", intensity)

camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, -300, 105))
camera.set_actor_location(unreal.Vector(0, -300, 105), False, True)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(0, 0, 0)), False)
view = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
view.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
view.set_editor_property("ortho_width", 300.0)
view.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
settings = view.get_editor_property("post_process_settings")
settings.set_editor_property("override_auto_exposure_method", True)
settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
settings.set_editor_property("override_auto_exposure_bias", True)
settings.set_editor_property("auto_exposure_bias", 0.0)
view.set_editor_property("post_process_settings", settings)
view.set_editor_property("capture_every_frame", False)
view.set_editor_property("capture_on_movement", False)
view.set_editor_property("always_persist_rendering_state", True)
target = unreal.RenderingLibrary.create_render_target2d(world, 1200, 800, unreal.TextureRenderTargetFormat.RTF_RGBA8)
view.set_editor_property("texture_target", target)
started = time.monotonic()
handle = None


def tick(delta):
    global handle
    try:
        view.capture_scene()
        if time.monotonic() - started < 12:
            return
        unreal.RenderingLibrary.export_render_target(world, target, str(out), "materials-preview-approved.png")
        (out / "preview.json").write_text(json.dumps({"preview": str(out / "materials-preview-approved.png"), "left": "M_Deco_Wood", "right": "M_Deco_Concrete", "saved_map": False}), encoding="utf-8")
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.execute_console_command(world, "QUIT_EDITOR")
    except Exception as error:
        (out / "preview-error.txt").write_text(str(error), encoding="utf-8")
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.execute_console_command(world, "QUIT_EDITOR")
        raise


handle = unreal.register_slate_post_tick_callback(tick)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
