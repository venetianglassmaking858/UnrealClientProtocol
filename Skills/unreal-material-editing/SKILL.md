---
name: unreal-material-editing
description: Edit UE material node graphs via text (ReadGraph/WriteGraph). Use when the user asks to add, remove, or rewire material expression nodes. For setting material properties like ShadingModel or BlendMode, use the unreal-object-operation skill instead.
---

# Material Editing

Two aspects: **material object properties** (ShadingModel, BlendMode, etc.) via `SetObjectProperty` from `unreal-object-operation` skill, and **node graph** (expression nodes and connections) via this skill's ReadGraph/WriteGraph.

**Prerequisite**: UE editor running with UCP plugin enabled.

## Material Object Properties

Use `SetObjectProperty` (see `unreal-object-operation` skill):

| Property | Example Values |
|----------|---------------|
| `ShadingModel` | `MSM_DefaultLit`, `MSM_Unlit`, `MSM_Subsurface`, `MSM_ClearCoat` |
| `BlendMode` | `BLEND_Opaque`, `BLEND_Masked`, `BLEND_Translucent`, `BLEND_Additive` |
| `MaterialDomain` | `MD_Surface`, `MD_DeferredDecal`, `MD_LightFunction`, `MD_PostProcess`, `MD_UI` |
| `TwoSided` | `true`, `false` |

## Node Graph API

CDO: `/Script/UnrealClientProtocolEditor.Default__MaterialGraphEditingLibrary`

| Function | Params | Description |
|----------|--------|-------------|
| `ListScopes` | `AssetPath` | Returns available scopes (output pin names, `Composite:` subgraphs) |
| `ReadGraph` | `AssetPath`, `ScopeName` | Returns text representation. Empty scope = all nodes. |
| `WriteGraph` | `AssetPath`, `ScopeName`, `GraphText` | Apply changes, returns diff. Always auto-relayouts after apply. |
| `Relayout` | `AssetPath` | Force auto-layout |

**Workflow**: ReadGraph → modify text → WriteGraph. Always read before write.

## Text Format

```
=== nodes ===
N<idx> <ClassName> {Key:Value, Key:Value} #<guid>

=== links ===
N<from>[.Output] -> N<to>.<Input>
N<from>[.Output] -> [MaterialOutput]
```

### Nodes

- `N<idx>`: local reference ID, 0-based
- `<ClassName>`: exact UE class name (e.g. `MaterialExpressionScalarParameter`)
- `{...}`: only non-default properties. Omit braces if none.
- `#<guid>`: 32-hex GUID. **Preserve for existing nodes, omit for new nodes.**

Property values: `"string"`, `0.5`, `true`, `(R=1.0,G=0.5,B=0.0,A=1.0)`, `"/Game/Textures/T_Diff"`, `SAMPLERTYPE_Color`. Copy unknown formats verbatim.

### Links

- Source: `N0` (single output) or `N0.RGB` (named output: `.R`, `.G`, `.B`, `.A`, `.RGB`, `.RGBA`)
- Target expression: `N1.InputName`
- Target material output: `[BaseColor]`, `[Roughness]`, `[EmissiveColor]`, `[Normal]`, etc.

## Common Expression Classes

| Class | Key Properties | Notes |
|-------|---------------|-------|
| `MaterialExpressionScalarParameter` | ParameterName, DefaultValue, Group | float param |
| `MaterialExpressionVectorParameter` | ParameterName, DefaultValue | color/vector param |
| `MaterialExpressionStaticBoolParameter` | ParameterName, DefaultValue | static bool |
| `MaterialExpressionTextureSample` | Texture | |
| `MaterialExpressionConstant` | R | float |
| `MaterialExpressionConstant2Vector` | R, G | |
| `MaterialExpressionConstant3Vector` | Constant | FLinearColor |
| `MaterialExpressionAdd/Subtract/Multiply/Divide` | | A, B |
| `MaterialExpressionLinearInterpolate` | | A, B, Alpha |
| `MaterialExpressionClamp` | | Input, Min, Max |
| `MaterialExpressionOneMinus` | | 1 - Input |
| `MaterialExpressionPower` | | Base, Exp |
| `MaterialExpressionDotProduct` | | A, B |
| `MaterialExpressionNormalize` | | |
| `MaterialExpressionAppendVector` | | A, B |
| `MaterialExpressionComponentMask` | R, G, B, A | swizzle |
| `MaterialExpressionTextureCoordinate` | CoordinateIndex, UTiling, VTiling | |
| `MaterialExpressionTime` | | game time |
| `MaterialExpressionWorldPosition` | | |
| `MaterialExpressionCameraPositionWS` | | |
| `MaterialExpressionFresnel` | Exponent, BaseReflectFraction | |
| `MaterialExpressionSaturate/Abs/Ceil/Floor/Frac/SquareRoot` | | single-input math |
| `MaterialExpressionSine/Cosine` | | Period property |
| `MaterialExpressionIf` | | pins: A, B, A>B, A==B, A<B |
| `MaterialExpressionCustom` | Code, OutputType, InputNames | Custom HLSL — see rules below |
| `MaterialExpressionComposite` | SubgraphName | subgraph container |

### Dynamic-input nodes

**Switch**: `{SwitchInputNames:["Red","Green","Blue"]}` — connect `N1 -> N0.SwitchValue`, `N2 -> N0.Default`, `N3 -> N0.Red`

**SetMaterialAttributes**: `{Attributes:["BaseColor","Roughness"]}` — input 0 is `MaterialAttributes`, then attribute pins by name.

**Custom**: `{InputNames:["UV","Scale"]}` — creates named input pins. Access in HLSL **by name directly**: `UV`, `Scale` (NOT `Inputs[0]`). Connect by name: `N1 -> N0.UV`.

## Custom HLSL Rules

**Only use `MaterialExpressionCustom` when native nodes cannot express the logic:**
- Loops (`for`/`while`), matrix ops (`float2x2`, `mul`), bitwise ops, complex branching

**Do NOT use for**: simple math, trig, texture sampling, frac/abs/saturate — use native nodes.

**When converting shaders (GLSL/ShaderToy):**
1. Decompose into logical blocks
2. Build data flow with native nodes (UV, Time, constants, math, lerps)
3. Custom HLSL only for irreducible blocks (a loop, a matrix transform)
4. Each Custom node does ONE thing — small and focused
5. **Never put an entire shader into one Custom HLSL node**

**Use `struct Functions` pattern** for HLSL helpers to avoid global namespace pollution:

```hlsl
struct Functions
{
    static float2 Rotate2D(float2 P, float Angle)
    {
        float C = cos(Angle); float S = sin(Angle);
        return float2(C*P.x - S*P.y, S*P.x + C*P.y);
    }
};
return Functions::Rotate2D(UV, Angle);
```

`OutputType`: `CMOT_Float1`, `CMOT_Float2`, `CMOT_Float3`, `CMOT_Float4`.

## Engine Material Utilities

These are UE engine built-in UFunctions (from `UMaterialEditingLibrary`). Call via UCP with CDO `/Script/MaterialEditor.Default__MaterialEditingLibrary`.

### Compile & Update

| Function | Params | Description |
|----------|--------|-------------|
| `RecompileMaterial` | `Material` | Recompile a material. Call after graph/property changes to see results in viewport. |
| `UpdateMaterialFunction` | `MaterialFunction` | Update a MaterialFunction and recompile all materials that reference it. |
| `UpdateMaterialInstance` | `Instance` | Recompile a MaterialInstanceConstant after parameter changes. |

### Material Instance Editing

| Function | Params | Description |
|----------|--------|-------------|
| `SetMaterialInstanceParent` | `Instance`, `NewParent` | Set parent material/instance. |
| `ClearAllMaterialInstanceParameters` | `Instance` | Clear all overridden parameters. |
| `SetMaterialInstanceScalarParameterValue` | `Instance`, `ParameterName`, `Value` | Set float parameter. |
| `SetMaterialInstanceVectorParameterValue` | `Instance`, `ParameterName`, `Value` | Set vector/color parameter. |
| `SetMaterialInstanceTextureParameterValue` | `Instance`, `ParameterName`, `Value` | Set texture parameter. |
| `SetMaterialInstanceStaticSwitchParameterValue` | `Instance`, `ParameterName`, `Value` | Set static switch parameter. |
| `GetMaterialInstanceScalarParameterValue` | `Instance`, `ParameterName` | Get float parameter value. |
| `GetMaterialInstanceVectorParameterValue` | `Instance`, `ParameterName` | Get vector/color parameter value. |
| `GetMaterialInstanceTextureParameterValue` | `Instance`, `ParameterName` | Get texture parameter value. |
| `GetMaterialInstanceStaticSwitchParameterValue` | `Instance`, `ParameterName` | Get static switch value. |

### Parameter Discovery

| Function | Params | Description |
|----------|--------|-------------|
| `GetScalarParameterNames` | `Material` | List all scalar parameter names. |
| `GetVectorParameterNames` | `Material` | List all vector parameter names. |
| `GetTextureParameterNames` | `Material` | List all texture parameter names. |
| `GetStaticSwitchParameterNames` | `Material` | List all static switch parameter names. |
| `GetMaterialDefaultScalarParameterValue` | `Material`, `ParameterName` | Get default scalar value from base material. |
| `GetMaterialDefaultVectorParameterValue` | `Material`, `ParameterName` | Get default vector value from base material. |
| `GetMaterialDefaultTextureParameterValue` | `Material`, `ParameterName` | Get default texture from base material. |
| `GetMaterialDefaultStaticSwitchParameterValue` | `Material`, `ParameterName` | Get default static switch value. |

### Usage & Statistics

| Function | Params | Description |
|----------|--------|-------------|
| `HasMaterialUsage` | `Material`, `Usage` | Check if a usage flag is enabled. |
| `SetBaseMaterialUsage` | `Material`, `Usage`, `bValue` | Enable/disable usage (e.g. `MATUSAGE_SkeletalMesh`). |
| `GetStatistics` | `Material` | Returns `FMaterialStatistics` (instruction count, samplers, etc.). |
| `GetNumShaderTypes` | `Material` | Number of compiled shader permutations. |
| `GetUsedTextures` | `Material` | List all textures referenced by the material. |
| `GetChildInstances` | `Parent` | Get all direct child MaterialInstances. |

### Relationships

| Function | Params | Description |
|----------|--------|-------------|
| `GetChildInstances` | `Parent` | Get all direct child material instances of a material. |
| `GetMaterialsReferencingFunction` | `InFunction` | Find all materials using a specific MaterialFunction. |

## Key Rules

1. **Prefer native nodes**. Custom HLSL only for irreducible logic (loops, matrix, bitwise). Use `struct Functions` pattern. **Never put entire effects in one Custom node.**
2. **Preserve GUIDs** on existing nodes. Omit for new nodes.
3. **Preserve class names and property formats exactly.**
4. **ReadGraph before WriteGraph** — always read first.
5. **Batch with UCP** — combine ListScopes + ReadGraph in one call.
6. All operations support **Undo** (Ctrl+Z).

## Error Handling

- Check `errors` array: unknown class, failed connection (wrong pin name), property import failure.
- Check `warnings` array for non-fatal issues.
- Re-read after write if unsure.
