# UE 5.8.4 BxDF integration notes

The first target is the installed UE 5.8.4 build. `DefaultLitBxDF` is in
`Engine/Shaders/Private/ShadingModels.ush`. It is also called by several
other legacy shading models; an override must be restricted to the intended
material and must not assume that every call represents Default Lit.

## Deferred rendering constraints

The material base pass and the light pass are compiled independently. A
Custom Expression's Additional Defines are emitted into generated material
code and do not automatically define macros in the deferred light shader.
Light direction is available during light evaluation, not as a general input
to an opaque deferred material's base-pass graph.

In this engine version, `SetSlotsForShadingModelType` and
`DetermineUsedMaterialSlots` in `ShaderGenerationUtil.cpp` do not allocate
the CustomData slot for Default Lit. `HasCustomGBufferData` in
`DeferredShadingCommon.ush` also excludes Default Lit. Merely assigning
`GBuffer.CustomData` in a patched shader therefore does not establish a
reliable material-to-light-pass channel. Shader slot generation is part of
the installed engine binary; editing its C++ source does not change that
binary. Any alternative packing must document which existing channels it
uses, their precision and the material features it displaces.

Per-material arbitrary HLSL evaluated with each light's direction is a
different requirement from passing a few material parameters to a shared
Toon BxDF. The latter can use bounded encoded data if a viable channel is
identified. The former needs a material-specific light evaluation path or a
renderer/compiler extension; a GBuffer cannot carry arbitrary code.

## Implemented per-light HLSL path

The requested material authoring contract is a connected Custom Expression
with Additional Define `MAZELINE_SHADER_OVERRIDE`, and the ordinary material
Base Color, Roughness and Metallic pins carry the authored inputs. The define must mark
only participating materials. In a deferred light pass it cannot be used as
the per-material switch directly: the base pass must encode a marker that the
shared light shader can decode. A connected graph input supplies its evaluated
value, not the HLSL expression or a callable function. The per-light function
therefore still needs a separate compilation/registration mechanism if it is
to receive the actual light direction, as requested.

`FDeferredLightPS` in `Renderer/Private/LightRendering.cpp` is a global
shader and its dispatch uses the view's global shader map. A material's
generated HLSL and Additional Defines are not automatically part of that
shader. In contrast, `TBasePassPS` is a mesh material shader, and
`BasePassPixelShader.usf` includes generated material HLSL before the forward
lighting code. The implemented Forward hook calls the included user function
inside `DefaultLitBxDF`, guarded by the material define and the Default Lit ID.
It supplies BaseColor, Metallic, N, V and `AreaLight.DiffuseL`, with inout
DiffuseColor and Roughness. Roughness is saturated after the user function.

The implemented Deferred path generates a global HLSL registry containing
the participating user functions plus a per-pixel selector. The selector
survives the base pass into the light pass using the existing two-bit
per-object GBuffer field, with shading-model ID 14 marking selected pixels.
Function IDs 1, 2 and 3 select separately compiled user functions. This
constrains what each function can read:
arbitrary textures, samplers, material graph values and per-instance data do
not become available in the global lighting shader automatically. It is not
equivalent to arbitrary material graph evaluation. Registry regeneration,
shader compilation, cooking, packaging and actual rendering have been tested
for Windows DX11/SM5 Opaque Default Lit in both rendering paths. This is not
a performance benchmark or a claim of support for every renderer feature.

The chosen direction permits dedicating an existing GBuffer channel for the
deferred path, but Metallic must remain available as a material input and
cannot be dedicated to the selector. A distinct marker is necessary to avoid changing
ordinary Default Lit pixels. The implementation reserves ID 14 for that
marker. In particular, `GBufferPostDecode` maps non-Default-Lit IDs to
Unlit under `FORCE_SIMPLE_SHADING` or `SUBSTRATE_FASTPATH`, and other engine
paths make shading-model-specific decisions. These fast paths and Substrate
are outside the supported scope; the test project disables Substrate. The
implemented ID must not be shared with another custom shading-model plugin.

The generated desktop layout declares the per-object GBuffer field as two
UNorm bits. Both are used: contact shadows and dynamic indirect shadow caster
representation. Reusing it for selected materials sacrifices those two
flags and provides only four values, and still requires an independent
marker distinguishing override pixels from ordinary Default Lit pixels. The
generic AO field is also occupied: depending on
configuration it contains ambient occlusion, indirect irradiance or indirect
sample occlusion. Reserving it would therefore change indirect-light behavior.
The per-object field was verified with the generated function registry;
the generic AO field is not modified. BaseColor, Metallic and Roughness
remain in their normal channels. Quantitative input, output and unmarked
material regression results are recorded in `Validation-Results.md`.

## Engine patch requirements (0.3.0)

`FMazelineShaderPatchManager` in the plugin's Private/MazelineShaderPatchManager.cpp
collects connected Custom Expressions, generates all three shaders in `Generate`,
and manages apply/update/restore/recovery without Python. The native SHA-256,
immutable snapshots and pending journal retain the Python format 1 layout.
Operations use the same engine operation.lock byte lock. Recovery accepts only
recorded old/new bytes and refuses external edits. Switching active project
owners requires restoring the previous patch. Backups survive restore.

Project Settings > Plugins > Mazeline Super Override Material exposes the manager
through a retained Slate widget. Current-editor operations finish shader compilation
and flush rendering commands before mutation, then invalidate the source cache and
require an editor restart. PIE and other editors/workers using the same engine
block mutation. The C++ MazelineShaderPatch commandlet supports null-RHI recovery
when a partial shader transaction prevents a normal renderer startup.

Disabling a material define only removes that material's selection; it does not
uninstall the global shader patch. Native migration tests reproduce the previously
GPU-validated shader bytes exactly. See Native-Patch-Validation.md for the native
manager tests and Validation-Results.md for historical rendering evidence.
