---
name: unreal-actor-editing
description: Manage actors in UE levels via UCP. Use when the user asks to spawn, delete, move, duplicate, select, or otherwise manipulate actors in the Unreal Engine editor.
---

# Unreal Actor Editing

Manage actors in Unreal Engine levels through UCP's `call` command. This skill guides you to use engine-provided actor subsystems and libraries.

**Prerequisite**: The `unreal-client-protocol` skill must be available and the UE editor must be running with the UCP plugin enabled.

## Custom Function Library (Placeholder)

### UActorEditorOperationLibrary (Editor)

**CDO Path**: `/Script/UnrealClientProtocolEditor.Default__ActorEditorOperationLibrary`

This library is currently a placeholder for future custom actor operations. For now, use the engine built-in libraries below.

## Engine Built-in Actor Libraries

### UEditorActorSubsystem

**CDO Path**: `/Script/UnrealEd.Default__EditorActorSubsystem`

This is the primary API for actor manipulation in the editor:

| Function | Description |
|----------|-------------|
| `GetAllLevelActors()` | Get all actors in the current level |
| `GetAllLevelActorsOfClass(ActorClass)` | Get all actors of a specific class |
| `GetSelectedLevelActors()` | Get currently selected actors |
| `SetSelectedLevelActors(ActorsToSelect)` | Set actor selection |
| `SelectNothing()` | Deselect all actors |
| `SpawnActorFromClass(ActorClass, Location, Rotation)` | Spawn a new actor |
| `SpawnActorFromObject(ObjectToUse, Location, Rotation)` | Spawn from an existing asset |
| `DuplicateActor(ActorToDuplicate, ToWorld, Offset)` | Duplicate an actor |
| `DuplicateActors(ActorsToDuplicate, ToWorld, Offset)` | Duplicate multiple actors |
| `DestroyActor(ActorToDestroy)` | Delete an actor |
| `DestroyActors(ActorsToDestroy)` | Delete multiple actors |
| `SetActorTransform(Actor, WorldTransform)` | Set actor transform |
| `ConvertActors(Actors, ActorClass)` | Convert actors to a different class |

#### Examples

**Get all actors in level:**
```json
{"type":"call","object":"/Script/UnrealEd.Default__EditorActorSubsystem","function":"GetAllLevelActors"}
```

**Get all static mesh actors:**
```json
{"type":"call","object":"/Script/UnrealEd.Default__EditorActorSubsystem","function":"GetAllLevelActorsOfClass","params":{"ActorClass":"/Script/Engine.StaticMeshActor"}}
```

**Spawn a point light:**
```json
{"type":"call","object":"/Script/UnrealEd.Default__EditorActorSubsystem","function":"SpawnActorFromClass","params":{"ActorClass":"/Script/Engine.PointLight","Location":{"X":0,"Y":0,"Z":200}}}
```

**Destroy an actor:**
```json
{"type":"call","object":"/Script/UnrealEd.Default__EditorActorSubsystem","function":"DestroyActor","params":{"ActorToDestroy":"/Game/Maps/Main.Main:PersistentLevel.PointLight_0"}}
```

### UGameplayStatics

**CDO Path**: `/Script/Engine.Default__GameplayStatics`

Useful for runtime/game queries:
- `GetAllActorsOfClass(WorldContextObject, ActorClass)` — Find actors by class
- `GetAllActorsWithTag(WorldContextObject, Tag)` — Find actors by tag
- `GetAllActorsOfClassWithTag(WorldContextObject, ActorClass, Tag)` — Combined filter
- `GetActorOfClass(WorldContextObject, ActorClass)` — Get first actor of class
- `GetPlayerPawn(WorldContextObject, PlayerIndex)` — Get player pawn
- `GetPlayerController(WorldContextObject, PlayerIndex)` — Get player controller
- `GetPlayerCameraManager(WorldContextObject, PlayerIndex)` — Get camera manager

### UEditorLevelLibrary

**CDO Path**: `/Script/EditorScriptingUtilities.Default__EditorLevelLibrary`

Additional level editing functions:
- `GetAllLevelActors()` — Get all level actors
- `GetSelectedLevelActors()` — Get selected actors
- `SetSelectedLevelActors(ActorsToSelect)` — Set selection
- `GetEditorWorld()` — Get the editor world
- `JoinStaticMeshActors(ActorsToJoin, JoinOptions)` — Merge static meshes
- `SetCurrentLevelByName(LevelName)` — Switch current level
- `SetActorSelectionState(Actor, bShouldBeSelected)` — Toggle actor selection
- `PilotLevelActor(ActorToPilot)` — Pilot an actor in viewport

## Common Patterns

### Discover and inspect actors

```json
[
  {"type":"call","object":"/Script/UnrealEd.Default__EditorActorSubsystem","function":"GetAllLevelActors"},
  {"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"GetObjectProperty","params":{"ObjectPath":"<actor_path>","PropertyName":"RelativeLocation"}}
]
```

### Move an actor

Use the `unreal-object-operation` skill to set properties:

```json
{"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"SetObjectProperty","params":{"ObjectPath":"<actor_root_component_path>","PropertyName":"RelativeLocation","JsonValue":"{\"X\":100,\"Y\":200,\"Z\":0}"}}
```

### Spawn, position, and select

```json
[
  {"type":"call","object":"/Script/UnrealEd.Default__EditorActorSubsystem","function":"SpawnActorFromClass","params":{"ActorClass":"/Script/Engine.PointLight","Location":{"X":0,"Y":0,"Z":300}}},
  {"type":"call","object":"/Script/UnrealEd.Default__EditorActorSubsystem","function":"GetSelectedLevelActors"}
]
```
