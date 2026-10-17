Free Surface Decals - 5 Pack
Taproot Solutions LLC

Five PBR decals, each a 1024x1024 set of four maps:

  <name>_albedo.png      colour, with opacity in the alpha channel
  <name>_normal.png      tangent-space normal, OpenGL convention (Y+)
  <name>_roughness.png   roughness, white = rough
  <name>_height.png      height, mid grey = the surface it sits on

  concrete-crack/    branching crack with chipped edges
  scorch-mark/       soot burn with blistered paint
  bullet-hole/       impact hole with spalling and radial cracks
  leak-stain/        water seeping down from a ragged source line
  paint-splatter/    red paint blob, flung droplets and drips

Every decal fades to fully transparent before the edge of its texture, so the
projector's square never shows. Outside the decal, the normal map is flat and
the roughness is neutral.

Every map is generated procedurally from noise and shape code.

Unity (URP)
  Add a Decal renderer feature to the URP Renderer. Create a material with
  the Shader Graphs/Decal shader, assign the albedo as Base Map and the
  normal as Normal Map (set the normal texture's Texture Type to Normal
  map), then place a Decal Projector and give it the material.

Unity (HDRP)
  Create a material with HDRP/Decal, assign the same maps, and use a Decal
  Projector.

Unreal Engine
  Set the material's Material Domain to Deferred Decal and Blend Mode to
  Translucent. Connect albedo RGB to Base Color, albedo alpha to Opacity,
  roughness to Roughness and the normal to Normal. Unreal uses the DirectX
  normal convention, so tick Flip Green Channel on the normal texture.
  Place it with a Decal Actor.

Godot 4
  Add a Decal node and assign the albedo, normal and roughness textures to its
  Albedo, Normal and ORM slots.

See LICENSE.txt for the terms.
