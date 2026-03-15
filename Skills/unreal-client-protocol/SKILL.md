---
name: unreal-client-protocol
description: Interact with the running Unreal Engine editor via TCP bridge. Use when the user asks to call UFunctions or perform any Unreal Engine operation. This is the transport layer — see domain-specific skills (unreal-object-operation, unreal-material-editing, etc.) for specific operations.
---

# UnrealClientProtocol

Communicate with a running UE editor through the UnrealClientProtocol TCP plugin. UCP exposes only two built-in commands: `batch` and `call`. All other functionality is provided through Blueprint Function Libraries that you invoke via `call`.

## Invocation

When you read this SKILL.md, you already know its absolute path. Replace the filename with `scripts/UCP.py` to get UCP.py's path. For example, if this file is at `X/Skills/unreal-client-protocol/SKILL.md`, then UCP.py is at `X/Skills/unreal-client-protocol/scripts/UCP.py`. **Do NOT search or glob for UCP.py.**

Use PowerShell **here-string** (`@'...'@`) to pipe JSON into UCP.py. This avoids all quote/escape issues:

```powershell
@'
{"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"FindObjectInstances","params":{"ClassName":"/Script/Engine.World"}}
'@ | python "<path-to-UCP.py>"
```

**IMPORTANT**: The `@'` must be on its own line, and `'@` must also be at the start of its own line. This is PowerShell here-string syntax — the content between them is passed verbatim with zero escaping needed.

**NEVER** use `echo '...'` or `echo "..."` for JSON in PowerShell — quotes and braces will be corrupted.

## Commands

### call — Call a UFunction

```json
{"type":"call","object":"<object_path>","function":"<func_name>","params":{...}}
```

- `object`: Full UObject path — use CDO path for static/library functions, instance path for member methods.
- `function`: The UFunction name exactly as declared in C++.
- `params`: (optional) Map of parameter name -> value. UObject* params accept path strings. Omit `WorldContextObject`.

### batch — Run multiple calls in one request

```json
[
  {"type":"call","object":"...","function":"...","params":{...}},
  {"type":"call","object":"...","function":"...","params":{...}}
]
```

Pass a JSON array to UCP.py — it wraps them in a batch automatically.

## Core Principle — "Knowledge First"

You (the AI) already possess extensive knowledge of the Unreal Engine C++ / Blueprint API. **Always leverage that knowledge to construct commands directly**. Use `DescribeObject` / `DescribeObjectFunction` only when uncertain about project-specific classes.

### Key Rules

1. **Construct from knowledge first.** You know UE APIs. Just call them.
2. **Batch aggressively.** Group independent calls into one JSON array to reduce round-trips.
3. **WorldContext is auto-injected.** Never pass `WorldContextObject` manually.
4. **Latent functions are not supported.** Functions with `FLatentActionInfo` will be rejected.
5. **Check the `log` field.** If the response contains a `log` array, warnings or errors occurred.
6. **Read error responses carefully.** A failed `call` returns `{"error":"...", "expected":{...}}` — use it to self-correct.

## Object Path Conventions

| Kind | Pattern | Example |
|------|---------|---------|
| Static/CDO | `/Script/<Module>.Default__<Class>` | `/Script/Engine.Default__KismetSystemLibrary` |
| Instance | `/Game/Maps/<Level>.<Level>:PersistentLevel.<Actor>` | `/Game/Maps/Main.Main:PersistentLevel.BP_Hero_C_0` |
| Class (for find) | `/Script/<Module>.<Class>` | `/Script/Engine.StaticMeshActor` |

**CDO class name convention:** Drop the `U` or `A` prefix. `UKismetSystemLibrary` -> `Default__KismetSystemLibrary`.

## Available Blueprint Function Libraries

UCP provides several function libraries accessible via `call`. Each has its own Skill for detailed documentation:

| Library | CDO Path | Skill | Purpose |
|---------|----------|-------|---------|
| `UObjectOperationLibrary` | `/Script/UnrealClientProtocol.Default__ObjectOperationLibrary` | `unreal-object-operation` | Object property R/W, reflection, instance search |
| `UObjectEditorOperationLibrary` | `/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary` | `unreal-object-operation` | Undo/Redo transactions |
| `UAssetEditorOperationLibrary` | `/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary` | `unreal-asset-operation` | Get AssetRegistry instance for asset queries |
| `UMaterialGraphEditingLibrary` | `/Script/UnrealClientProtocolEditor.Default__MaterialGraphEditingLibrary` | `unreal-material-editing` | Material graph read/write |
| `UActorEditorOperationLibrary` | `/Script/UnrealClientProtocolEditor.Default__ActorEditorOperationLibrary` | `unreal-actor-editing` | Actor operations (placeholder) |

## Response Format

- **Success**: Returns the result value directly, no wrapper.
- **Failure**: Returns `{"error":"...", "expected":{...}}` where `expected` contains the function signature.
- **Batch**: Returns an array, each element simplified independently.
- **Log**: If warnings/errors occurred, a `log` field (string array) is appended.
- **Log level**: Add `"log_level":"all"` to any request to capture all log levels (default captures Warning+). Options: `"all"`, `"log"`, `"display"`, `"warning"` (default), `"error"`.
