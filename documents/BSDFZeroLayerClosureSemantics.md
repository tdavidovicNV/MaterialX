# Proposal: Zero-Weighted BSDF Layer Semantics

Status: proposal
Audience: MaterialX maintainers and backend owners
Scope: MaterialX PBS closure semantics, specification wording, and GLSL conformance

## Summary

MaterialX should define a null or empty BSDF distribution value and make finite zero weighting of BSDF and EDF distributions produce that empty distribution. Under this closure-algebra rule:

```text
layer(multiply(top, 0), base) == base
```

This is the intended behavior when `multiply(top, 0)` produces the null BSDF before `layer` consumes it. The rule is consistent with OSL-style closure algebra, with the MaterialX PBR description of `multiply` as contribution scaling, and with common production material models where a zero coat, top layer, or component weight means the component is absent.

This proposal is not a claim about all physical slab models. A physically present top slab with IOR, thickness, absorption, transmission, volume scattering, or refraction can affect the base even if a particular reflected lobe is zero. Those effects should remain explicit controls in the material model. Opacity, presence, cutout, displacement, physical thickness, VDF or volume state, and other non-BSDF controls must not be erased by pruning a zero BSDF closure.

## Problem Statement

The current GLSL backend has internally inconsistent semantics for BSDF `weight=0`, `multiply(bsdf, 0)`, and `layer`:

* GLSL represents a BSDF as `response` plus `throughput`, and the default BSDF is `BSDF(vec3(0.0), vec3(1.0))`, which already corresponds to an empty response with identity throughput. See [`GlslSyntax.cpp`](../source/MaterialXGenGlsl/GlslSyntax.cpp).
* `mx_layer_bsdf` evaluates `top.response + base.response * top.throughput`, so top throughput directly controls how much base survives. See [`mx_layer_bsdf.glsl`](../libraries/pbrlib/genglsl/mx_layer_bsdf.glsl).
* `mx_multiply_bsdf_float` and `mx_multiply_bsdf_color3` scale only `response` and preserve the incoming `throughput`. A zero multiplier therefore creates a zero-response top that may still attenuate the base. See [`mx_multiply_bsdf_float.glsl`](../libraries/pbrlib/genglsl/mx_multiply_bsdf_float.glsl) and [`mx_multiply_bsdf_color3.glsl`](../libraries/pbrlib/genglsl/mx_multiply_bsdf_color3.glsl).
* Direct `weight=0` behavior varies by node. Some nodes initialize throughput to zero while multiplying response by weight, so a zero-weight top can erase the base if layered. Other layerable nodes compute throughput as `1 - reflectance * weight`, so a zero weight passes the base through.
* OSL generation uses closure multiplication directly, for example `({{in2}} * {{in1}})` for BSDF multiply and `layer({{top}}, {{base}})` for layering. See [`pbrlib_genosl_impl.mtlx`](../libraries/pbrlib/genosl/pbrlib_genosl_impl.mtlx). In OSL closure algebra, zero multiplication yields the null closure.

This means two graph shapes that should be equivalent under distribution-function composition can differ in GLSL:

```text
top with weight = 0
multiply(top, 0)
layer(multiply(top, 0), base)
```

The issue is not that GLSL has a `throughput` field; that field is a reasonable approximation device for vertical layering. The issue is that `response` and `throughput` are not reset together when a closure has been explicitly scaled to the empty distribution.

## Current Evidence

### OSL Closure Algebra

OSL defines closures as symbolic distribution values. Its closure type supports scalar and color multiplication, addition, and assignment of `0` as the null closure. OSL's standard material library also defines `layer(top, base)` using the same approximate form as MaterialX: `base * (1 - reflectance(top)) + top`. See the OSL closure documentation: <https://open-shading-language.readthedocs.io/en/latest/datatypes.html#closures> and OSL material closures: <https://open-shading-language.readthedocs.io/en/v1.14.5.0/stdlib.html#material-closures>.

MaterialX's generated OSL follows those rules:

* BSDF node weights are emitted as scalar closure multiplication, for example `weight * dielectric_bsdf(...)`.
* `multiply` for BSDF and EDF is emitted as `in2 * in1`.
* `layer` is emitted as OSL `layer(top, base)`.

Under this model, `multiply(top, 0)` is the null closure. Passing that value to `layer` should not attenuate or otherwise modify the base closure.

### MaterialX PBR Specification

The MaterialX PBR specification already points toward this interpretation:

* BSDF, EDF, and VDF are distribution-function data types. See [`MaterialX.PBRSpec.md`](Specification/MaterialX.PBRSpec.md#data-types).
* `multiply` is described as multiplying the contribution of a distribution function by a scaling weight. See [`multiply`](Specification/MaterialX.PBRSpec.md#node-multiply).
* `layer` is described as target-specific vertical layering, with albedo scaling commonly written as `base * (1 - reflectance(top)) + top`. See [`layer`](Specification/MaterialX.PBRSpec.md#node-layer).
* `surface` exposes `opacity` separately from the BSDF input, so cutout or presence behavior is not encoded by the BSDF closure alone. See [`surface`](Specification/MaterialX.PBRSpec.md#node-surface).

If the top BSDF is empty, then `reflectance(empty) = 0`, and the approximate layer formula reduces to `base`.

### Current GLSL Implementation

The GLSL backend's BSDF struct is:

```glsl
struct BSDF { vec3 response; vec3 throughput; };
```

The default value is `BSDF(vec3(0.0), vec3(1.0))`, which is a natural representation of an empty BSDF for layer purposes.

However, `mx_multiply_bsdf_float` currently does this:

```glsl
result.response = in1.response * weight;
result.throughput = in1.throughput;
```

and `mx_multiply_bsdf_color3` does the same with a color multiplier. Consequently, `layer(multiply(top, 0), base)` can still observe `top.throughput` and attenuate the base even though the top response has been removed.

Direct BSDF `weight=0` behavior is also inconsistent. Layerable dielectric, generalized Schlick, and sheen BSDFs compute throughput using the weight, so zero weight gives identity throughput. Other BSDF nodes initialize throughput to zero and only scale response by weight, so zero weight does not necessarily produce the same empty value.

### Other Material Models

The broader production convention is that a zero layer or lobe weight means the layer or lobe is absent unless a separate control explicitly says otherwise:

* OpenPBR defines coat, fuzz, thin-film, transmission, and related weights as presence or mixture controls. The MaterialX OpenPBR nodegraph exposes those controls separately. See the OpenPBR specification: <https://academysoftwarefoundation.github.io/OpenPBR/> and [`open_pbr_surface.mtlx`](../libraries/bxdf/open_pbr_surface.mtlx).
* Autodesk Standard Surface uses a `coat` weight for the coat layer and has separate opacity and transmission concepts. See the Standard Surface specification: <https://autodesk.github.io/standard-surface/> and [`standard_surface.mtlx`](../libraries/bxdf/standard_surface.mtlx).
* Blender's Principled BSDF describes coat weight as controlling the intensity of the coat layer, with alpha handled separately. See <https://docs.blender.org/manual/en/latest/render/shader_nodes/shader/principled.html>.
* glTF `KHR_materials_clearcoat` defines `clearcoatFactor` with default `0.0`, and states that zero disables the whole clearcoat layer. See <https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_clearcoat/README.md>.
* Disney's principled BRDF notes describe clearcoat as a weighted extra lobe, with zero effectively disabling the layer. See <https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf>.
* Dassault Systemes Enterprise PBR and related layered models similarly separate clearcoat or layer weights from opacity and volume-like controls. See <https://dassaultsystemes-technology.github.io/EnterprisePBRShadingModel/spec-2025x.md.html>.

Physical layered-material literature, including PBRT and layered BSDF papers, remains relevant but addresses a different problem: modeling explicit optical slabs and inter-layer transport. See PBRT: <https://www.pbr-book.org/>; Weidlich and Wilkie: <https://www.cg.tuwien.ac.at/research/publications/2007/weidlich_2007_almfs/>; and Jakob et al.: <https://www.cs.cornell.edu/projects/layered-sg14/>. These models justify preserving explicit physical slab controls, not preserving response-only zeroing in a closure algebra backend.

Legacy Falcor 1.38 behavior can be cited as precedent and implementation opinion where useful, but it should not be treated as normative truth for MaterialX. The normative source of this proposal is closure algebra plus MaterialX's own distribution-function semantics.

## Proposed Specification Tightening

Add precise language to the PBS specification for BSDF and EDF distribution values:

1. Define the empty BSDF distribution value.

   The empty BSDF has zero response, zero directional reflectance, and identity layer throughput. It represents the absence of a BSDF contribution, not a physically present transparent slab.

2. Define the empty EDF distribution value.

   The empty EDF has zero emission. It represents the absence of an emission contribution.

3. Define finite zero multiplication.

   Multiplying a BSDF or EDF distribution by a finite zero scalar produces the empty distribution of that type. Multiplying by a finite zero color, meaning all color channels are finite zero, produces the empty distribution of that type. Partial color zeros continue to mean component-wise attenuation unless a future spectral or color semantics update says otherwise.

4. Define empty BSDF reflectance.

   `reflectance(empty BSDF) = 0`.

5. Define empty-top layer identity.

   `layer(empty, base) = base` for a BSDF base. If a target supports BSDF-over-VDF layering, the same identity should apply to the top BSDF being empty unless the VDF specification defines a different volume-coupled behavior.

6. Define weighted layer identity.

   For implementations or target languages with a weighted layer primitive, `weighted_layer(top, base, 0) = base` for finite zero weights.

7. Separate BSDF pruning from non-BSDF controls.

   These rules do not remove independent opacity, presence, cutout, displacement, geometry, normal, thickness, thin-walled, volume, VDF, or medium controls. If a node or material model has explicit non-BSDF state, that state must either be represented outside the pruned BSDF closure or be specified as part of the closure before pruning is allowed.

8. Do not generalize VDF semantics yet.

   This proposal does not define zero multiplication, layering identity, or pruning behavior for VDFs beyond existing specification text. Volume and medium semantics should be tightened separately.

9. Leave non-finite values unspecified for now.

   NaN and infinite weights should be an explicit open question. Backends may clamp, sanitize, or propagate according to existing target-language practice until the specification says otherwise.

## Proposed GLSL Implementation Direction

This proposal does not implement code, but it recommends the following backend direction.

### Represent Empty BSDF Consistently

GLSL already has a suitable empty BSDF representation:

```glsl
BSDF(vec3(0.0), vec3(1.0))
```

The GLSL backend should use this consistently for the empty BSDF. If maintainers prefer an explicit validity or closure-kind flag in the future, that would also be acceptable, but the essential property is that `layer` sees an empty top as identity over the base.

### Fix BSDF Multiply

`mx_multiply_bsdf_float` should reset throughput to identity when the effective scalar multiplier is finite zero:

```text
if finite_zero(weight):
    result = empty_bsdf
else:
    result.response = in1.response * weight
    result.throughput = appropriate_scaled_or_existing_throughput
```

For `mx_multiply_bsdf_color3`, a finite zero color `(0, 0, 0)` should produce the empty BSDF. For nonzero colors, maintainers should decide whether current throughput preservation is the best approximation or whether throughput should be recomputed from an explicit reflectance representation. The immediate conformance issue is the all-zero multiplier.

An alternative is to represent an explicit empty BSDF sentinel and make `layer` test it. That is less attractive than making `multiply` return the normal empty BSDF representation, but it is semantically equivalent if all composition nodes observe it consistently.

### Make Direct `weight=0` Consistent

Direct BSDF node implementations should return the defined empty BSDF when `weight` is finite zero, unless the node is explicitly specified to carry independent non-BSDF effects. This should be consistent across diffuse, translucent, subsurface, conductor, dielectric, generalized Schlick, sheen, hair, and future BSDF nodes.

For nodes whose GLSL implementation currently initializes throughput to zero, maintainers should either:

* branch the finite-zero weight case to return the empty BSDF, or
* express throughput in terms of weighted reflectance so zero weight naturally yields identity throughput.

### Keep Non-BSDF Effects Alive

Opacity and displacement are already separate shader inputs in MaterialX and should remain so. If future layer nodes expose physical thickness, absorption, or explicit medium state independently from the BSDF distribution value, those controls must not be dropped merely because a BSDF lobe weight is zero. The implementation should make that ownership visible in the graph or in the generated target code.

## Conformance Test Matrix

The test suite should include numeric GLSL/backend conformance tests and, where possible, cross-backend equivalence tests against generated OSL. Suggested cases:

| Case | Graph | Expected result |
| --- | --- | --- |
| Direct diffuse zero | `layer(diffuse(weight=0), base)` | `base` if diffuse is treated as an empty top; otherwise the node must document why it is not layerable as a top |
| Direct dielectric zero | `layer(dielectric(weight=0), base)` | `base` |
| Direct generalized Schlick zero | `layer(generalized_schlick(weight=0), base)` | `base` |
| Direct sheen zero | `layer(sheen(weight=0), base)` | `base` |
| Multiply float zero | `layer(multiply(top, 0.0), base)` | `base` |
| Multiply color zero | `layer(multiply(top, color3(0,0,0)), base)` | `base` |
| Weighted layer zero | `weighted_layer(top, base, 0.0)` or target equivalent | `base` |
| Nonzero top | `layer(multiply(top, 0.5), base)` | Top contributes and base attenuation follows the chosen approximation |
| Surface opacity | `surface(bsdf=multiply(top,0), opacity=0.25)` | Opacity remains 0.25; BSDF pruning does not rewrite cutout |
| Displacement | `surfacematerial(surface=..., displacement=...)` with empty BSDF | Displacement remains connected and effective |
| Volume or VDF | BSDF zeroing with a separately connected volume/VDF | Volume behavior remains governed by the volume/VDF spec, not by BSDF pruning |
| Physical slab controls | Explicit thickness or absorption controls, when present | Controls remain live unless their owning node explicitly says zero BSDF weight disables them |
| Non-finite weight | NaN or infinity | Mark expected behavior as backend-defined until specified |

For rendered image tests, the critical equality is:

```text
render(layer(multiply(top, 0), base)) == render(base)
```

within the normal tolerance for the backend and lighting setup. The top should use a BSDF with nontrivial Fresnel or throughput when unweighted so the test catches response-only zeroing.

## Migration Plan

1. Update the MaterialX PBR specification with the null BSDF and zero multiplication language.
2. Add or update backend-independent conformance assets expressing the graph identities above.
3. Update GLSL BSDF multiply to return the empty BSDF for finite zero scalar and all-zero color multipliers.
4. Audit direct GLSL BSDF node `weight=0` behavior and make all layerable BSDF nodes return the empty BSDF for finite zero weight unless specifically documented otherwise.
5. Add GLSL tests for direct zero weight, multiply-zero, layer identity, weighted-layer identity where applicable, and preservation of independent non-BSDF controls.
6. Compare generated OSL behavior for the same graphs. The expected OSL result is already the null-closure identity.
7. Document the compatibility change in release notes, including the specific GLSL cases where `layer(multiply(top, 0), base)` previously attenuated the base.

## Compatibility and Risk

This intentionally changes current GLSL behavior for graphs where a zero-multiplied top BSDF preserved non-identity throughput and therefore attenuated the base. That behavior is best understood as a backend inconsistency: the response has been removed, but the layer still observes transport state from the removed closure.

The change aligns GLSL with:

* MaterialX's `multiply` language as contribution scaling.
* MaterialX's `layer` formula when `reflectance(empty) = 0`.
* OSL closure algebra and generated MaterialX OSL.
* Production material model expectations for zero coat, top, or component weight.

The main risk is that some existing GLSL users may have relied on response-only zeroing as a way to preserve top-layer attenuation without top-layer response. That use should be represented explicitly with a physical slab, transmission, absorption, opacity, volume, or other non-BSDF control, not by relying on multiply-zero preserving hidden throughput. Release notes and tests should call this out clearly.

## Open Questions

* VDF semantics: Should `multiply(vdf, 0)` define an empty VDF, and if so what are its transmission and medium semantics?
* Physical thickness controls: Which existing or planned MaterialX nodes own slab thickness, absorption, and refraction state independently from BSDF closure weight?
* MDL weighted layers: How should MaterialX map the proposed identity to MDL constructs that may separate layer weight, Fresnel terms, and scattering state?
* Partial color weights: Should a BSDF multiplied by `color3(0, 1, 1)` have component-wise response scaling only, or should throughput become spectral or channel-aware?
* Non-finite inputs: Should NaN or infinity be clamped, sanitized, treated as an error, or left target-defined?
* Direct node exceptions: Are there any BSDF nodes that should intentionally carry independent non-BSDF effects at `weight=0`? If so, those exceptions should be explicit in the node documentation.

## Proposed Normative Text Sketch

The following is a starting point for specification wording:

> The PBS library defines an empty BSDF distribution value, representing no surface scattering contribution. The empty BSDF has zero reflectance and acts as the identity top layer: layering an empty BSDF over a BSDF base returns the base distribution.

> Multiplying a BSDF or EDF distribution by a finite zero scalar, or by a finite all-zero color, produces the empty distribution of the corresponding type. This rule applies to distribution-function composition only. It does not remove independently authored opacity, presence, displacement, geometry, volume, VDF, physical thickness, or medium controls.

> For the standard vertical layer approximation `base * (1 - reflectance(top)) + top`, `reflectance(empty BSDF)` is defined to be zero, so `layer(empty, base) = base`.

This wording should be integrated into the data type and utility node sections of the PBS specification, with cross-references from `multiply`, `layer`, and `surface`.
