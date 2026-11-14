"""Fresh-process verification and offscreen preview. Saves no map or existing asset."""
import json
import runpy
import time
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
out = project / "Saved/Automation/SurfaceSet"
# Fresh loads can return placeholder dimensions until texture compilation completes.
for kind in ("DecoWood", "WallBrick", "DecoConcrete", "WallConcrete", "WallWood"):
    for suffix in ("BaseColor", "Normal"):
        unreal.load_asset(f"/BreachSystem/SurfaceSet/Textures/T_{kind}_{suffix}")
startup_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(startup_world, "Editor.AsyncTextureCompilationFinishAll")
runpy.run_path(str(project / "Plugins/BreachSystem/Tools/create_surface_set.py"))
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
unreal.SystemLibrary.execute_console_command(world, "r.Streaming.FullyLoadUsedTextures 1")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
plane = unreal.load_asset("/Engine/BasicShapes/Plane")
rotation = unreal.MathLibrary.make_rot_from_xz(unreal.Vector(1, 0, 0), unreal.Vector(0, -1, 0))
kinds = ("DecoWood", "WallBrick", "DecoConcrete", "WallConcrete", "WallWood")
for index, kind in enumerate(kinds):
    material = unreal.load_asset(f"/BreachSystem/SurfaceSet/Materials/M_{kind}")
    if not material:
        raise RuntimeError(f"Missing material: {kind}")
    location = unreal.Vector((2 - index) * 125, 0, 0)
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, location, rotation)
    actor.set_actor_location(location, False, True)
    actor.set_actor_rotation(rotation, False)
    component = actor.static_mesh_component
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_static_mesh(plane)
    component.set_material(0, material)
    actor.set_actor_scale3d(unreal.Vector(1.12, 1.12, 1))
    if actor.get_actor_up_vector().y >= 0:
        raise RuntimeError("Preview plane faces away from camera")

for position, intensity in ((unreal.Vector(-300, -230, 250), 4.5), (unreal.Vector(300, -200, -60), 2.0)):
    light_rotation = unreal.MathLibrary.find_look_at_rotation(position, unreal.Vector(0, 0, 0))
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, position, light_rotation)
    light.set_actor_rotation(light_rotation, False)
    light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.light_component.set_editor_property("intensity", intensity)

camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, -650, 65))
camera.set_actor_location(unreal.Vector(0, -650, 65), False, True)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(0, 0, 0)), False)
view = camera.get_component_by_class(unreal.SceneCaptureComponent2D)
view.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
view.set_editor_property("ortho_width", 650.0)
view.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
settings = view.get_editor_property("post_process_settings")
for prop, value in (("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("override_auto_exposure_apply_physical_camera_exposure", True), ("auto_exposure_apply_physical_camera_exposure", False), ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.0)):
    settings.set_editor_property(prop, value)
view.set_editor_property("post_process_settings", settings)
view.set_editor_property("capture_every_frame", False)
view.set_editor_property("capture_on_movement", False)
view.set_editor_property("always_persist_rendering_state", True)
target = unreal.RenderingLibrary.create_render_target2d(world, 1800, 600, unreal.TextureRenderTargetFormat.RTF_RGBA8)
view.set_editor_property("texture_target", target)
started = time.monotonic()
handle = None


def tick(delta):
    global handle
    try:
        view.capture_scene()
        if time.monotonic() - started < 20:
            return
        unreal.RenderingLibrary.export_render_target(world, target, str(out), "materials-preview.png")
        (out / "preview.json").write_text(json.dumps({"preview": str(out / "materials-preview.png"), "left_to_right": list(kinds), "saved_map": False}), encoding="utf-8")
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.execute_console_command(world, "QUIT_EDITOR")
    except Exception as error:
        (out / "preview-error.txt").write_text(str(error), encoding="utf-8")
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.execute_console_command(world, "QUIT_EDITOR")
        raise


handle = unreal.register_slate_post_tick_callback(tick)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
