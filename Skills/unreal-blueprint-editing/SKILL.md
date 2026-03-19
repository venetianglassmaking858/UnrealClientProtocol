---
name: unreal-blueprint-editing
description: Edit UE Blueprint node graphs via text (ReadGraph/WriteGraph). Use when the user asks to add, remove, or rewire Blueprint nodes, create/modify functions, events, or variables in a Blueprint graph.
---

# Blueprint Editing

Edit Blueprint node graphs via a text-based representation. For setting object properties on the Blueprint asset itself, use the `unreal-object-operation` skill.

**Prerequisite**: UE editor running with UCP plugin enabled.

## Node Graph API

CDO: `/Script/UnrealClientProtocolEditor.Default__BlueprintGraphEditingLibrary`

| Function | Params | Description |
|----------|--------|-------------|
| `ListScopes` | `AssetPath` | Returns available graphs (EventGraph, Function:Name, Macro:Name) |
| `ReadGraph` | `AssetPath`, `ScopeName` | Returns text representation of a graph scope |
| `WriteGraph` | `AssetPath`, `ScopeName`, `GraphText` | Apply changes, returns diff. Auto-recompiles after apply. |

## Reading Strategy

**CRITICAL: Never read all scopes at once.** Blueprints can have dozens of functions/macros. Follow this strategy:

1. **ListScopes first** — get the outline of all available graphs
2. **Identify relevant scopes** — based on the task, pick only the scopes you need
3. **ReadGraph one scope at a time** — read only what you need to understand or modify
4. **Summarize before reading more** — after reading a scope, decide if you need more context

**Example workflow for understanding a Blueprint:**

```
Step 1: ListScopes → get ["EventGraph", "Function:Initialize", "Function:Capture", "Function:SaveRT", "Macro:UpdatePreview", ...]
Step 2: Based on user's task, pick 1-3 most relevant scopes
Step 3: ReadGraph for those scopes only
Step 4: If you need more context, read additional scopes one at a time
```

**Example workflow for modifying a Blueprint:**

```
Step 1: ListScopes → identify which scope contains the target logic
Step 2: ReadGraph for that single scope
Step 3: Modify the text
Step 4: WriteGraph for that scope
```

**Bulk operations** — for multiple scopes, use a Python script to call ListScopes, then iterate and call ReadGraph/WriteGraph for each scope as needed.

## Scope Names

| Scope Format | Description |
|-------------|-------------|
| `EventGraph` | Main event graph (or any UbergraphPage name) |
| `Function:<Name>` | A function graph |
| `Macro:<Name>` | A macro graph |
| *(empty)* | Defaults to first UbergraphPage |

Names with spaces use underscores: `Function:My_Function`. The system matches with space/underscore equivalence.

## Text Format

```
=== scope: EventGraph ===

=== nodes ===
N<idx> <ClassName> {Key:Value, Key:Value} #<guid>

=== links ===
N<from>.<OutputPin> -> N<to>.<InputPin>
```

### Nodes

- `N<idx>`: local reference ID, 0-based
- `<ClassName>`: semantic encoding (see below) or raw UE class name
- `{...}`: non-default properties (reflection-serialized). Omit braces if none.
- `#<guid>`: 32-hex GUID. **Preserve for existing nodes, omit for new nodes.**

### ClassName Encoding

Common node types use semantic names for readability:

| Text Format | UE Node Type | Example |
|-------------|-------------|---------|
| `CallFunction:<Class>.<Func>` | `UK2Node_CallFunction` (external) | `CallFunction:KismetSystemLibrary.PrintString` |
| `CallFunction:<Func>` | `UK2Node_CallFunction` (self) | `CallFunction:MyBlueprintFunction` |
| `VariableGet:<Var>` | `UK2Node_VariableGet` | `VariableGet:Health` |
| `VariableSet:<Var>` | `UK2Node_VariableSet` | `VariableSet:Health` |
| `CustomEvent:<Name>` | `UK2Node_CustomEvent` | `CustomEvent:OnDamageReceived` |
| `Event:<Name>` | `UK2Node_Event` | `Event:ReceiveBeginPlay` |
| `FunctionEntry` | `UK2Node_FunctionEntry` | (auto-generated, cannot create/delete) |
| `FunctionResult` | `UK2Node_FunctionResult` | (auto-generated, cannot create/delete) |

Subclasses (e.g. `K2Node_CallParentFunction`, `K2Node_ComponentBoundEvent`) use their raw UE class name, fully handled by reflection.

Other node types use their raw class name (e.g. `K2Node_IfThenElse`, `K2Node_ForEachLoop`).

**Names with spaces use underscores**: `CallFunction:My_Custom_Function`, `VariableGet:Player_Health`.

### Properties

Properties are serialized via UE reflection. Pin default values use `pin.<PinName>` prefix.

Example: `{pin.InString:"Hello World", pin.Duration:5.0}`

### Links

All connections are node-to-node:

```
N0.then -> N1.execute          # exec flow
N2.ReturnValue -> N3.Condition  # data flow
```

Pin names match UE's internal `PinName`. Common exec pins: `execute`, `then`.

## Pseudocode Translation — Understand Before Modifying

**CRITICAL: After ReadGraph, always translate the NodeCode into pseudocode before making changes.** NodeCode describes structure (nodes, properties, links), not logic. You must reconstruct the logic to avoid breaking it.

### Why this matters

NodeCode text like:

```
N0 Event:ReceiveBeginPlay #aabb...
N1 CallFunction:SetupCamera #ccdd...
N2 VariableGet:CameraDistance #eeff...
N3 CallFunction:KismetMathLibrary.FClamp {pin.Min:100, pin.Max:5000} #1122...
N0.then -> N1.execute
N2.ReturnValue -> N3.Value
N3.ReturnValue -> N1.Distance
```

Should be understood as:

```
// BeginPlay:
//   ClampedDist = Clamp(CameraDistance, 100, 5000)
//   SetupCamera(Distance = ClampedDist)
```

### Workflow

1. **ReadGraph** — get NodeCode text
2. **Translate to pseudocode** — trace exec flow from entry nodes, resolve data flow into expressions
3. **Reason about the change** — use pseudocode to understand what needs to change
4. **Edit the NodeCode** — make structural changes with full understanding of the logic
5. **WriteGraph** — apply

### Use a SubAgent for translation

When the NodeCode output is large (20+ nodes) or the logic is complex, **launch a SubAgent** to do the translation. This keeps the main context clean and avoids losing track of the task goal amid raw NodeCode text.

**SubAgent prompt template:**

> Translate the following Blueprint NodeCode into pseudocode. Trace execution flow from entry nodes, resolve all data dependencies into inline expressions, and identify the high-level logic pattern.
>
> ```
> (paste NodeCode here)
> ```
>
> Return:
> 1. Pseudocode (one line per statement, with comments for non-obvious logic)
> 2. A brief summary: what this scope does, what it reads, what it writes, what it calls

After the SubAgent returns, use its pseudocode to reason about modifications in the main context.

### How to translate (if doing it yourself)

1. Find all entry nodes (Event, CustomEvent, FunctionEntry)
2. Follow `then`/`execute` exec links to build statement order
3. At each node, resolve data inputs by tracing backwards through data links
4. Collapse chains of math/utility nodes into inline expressions
5. Branch nodes → `if/else`, ForEachLoop → `for`, Sequence → sequential blocks

## Key Rules

1. **Read on-demand, not all at once.** ListScopes first, then ReadGraph only the scopes you need.
2. **Translate to pseudocode after reading** — understand the logic before editing.
3. **Preserve GUIDs** on existing nodes. Omit for new nodes.
4. **ReadGraph before WriteGraph** — always read the target scope before writing.
5. **FunctionEntry and FunctionResult cannot be created or deleted.**
6. All operations support **Undo** (Ctrl+Z).
7. **Incremental diff** — only changed nodes/links are modified; unchanged connections are preserved.

## Error Handling

- Check `diff` object in response: `nodes_added`, `nodes_removed`, `nodes_modified`, `links_added`, `links_removed`.
- Property errors: `"Property not found: ..."` or `"Failed to import ..."`.
- Pin errors: `"Pin not found: ..."`.
- Re-read after write if unsure.
