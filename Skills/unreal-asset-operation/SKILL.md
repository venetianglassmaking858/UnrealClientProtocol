---
name: unreal-asset-operation
description: Query and manage UE assets via UCP. Use when the user asks about asset search, dependencies, referencers, asset CRUD, asset management, getting selected/current assets, opening/closing asset editors, or any editor asset operation in Unreal Engine.
---

# Unreal Asset Operations

Operate on Unreal Engine assets through UCP's `call` command. The core approach is to obtain the `IAssetRegistry` instance and call its rich API directly, combined with engine asset libraries for CRUD operations.

**Prerequisite**: The `unreal-client-protocol` skill must be available and the UE editor must be running with the UCP plugin enabled.

## Custom Function Library

### UAssetEditorOperationLibrary (Editor)

**CDO Path**: `/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary`

| Function | Params | Returns | Description |
|----------|--------|---------|-------------|
| `GetAssetRegistry` | (none) | `IAssetRegistry` object | Returns the AssetRegistry instance. Call its functions directly via UCP `call`. |

#### Getting the AssetRegistry

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GetAssetRegistry"}
```

The returned object path can then be used as the `object` parameter for subsequent calls to IAssetRegistry functions.

Alternatively, you can use the engine helper directly:

```json
{"type":"call","object":"/Script/AssetRegistry.Default__AssetRegistryHelpers","function":"GetAssetRegistry"}
```

## IAssetRegistry — Full API

Once you have the AssetRegistry instance, you can call these BlueprintCallable functions on it:

### Asset Queries

| Function | Key Params | Description |
|----------|------------|-------------|
| `GetAssetsByPackageName` | `PackageName`, `OutAssetData`, `bIncludeOnlyOnDiskAssets` | Get assets in a specific package |
| `GetAssetsByPath` | `PackagePath`, `OutAssetData`, `bRecursive` | Get assets under a content path |
| `GetAssetsByPaths` | `PackagePaths`, `OutAssetData`, `bRecursive` | Get assets under multiple paths |
| `GetAssetsByClass` | `ClassPathName`, `OutAssetData`, `bSearchSubClasses` | Get all assets of a specific class |
| `GetAssets` | `Filter`, `OutAssetData` | Query with an `FARFilter` (powerful filtered search) |
| `GetAllAssets` | `OutAssetData`, `bIncludeOnlyOnDiskAssets` | Get ALL registered assets |
| `GetAssetByObjectPath` | `ObjectPath` | Get single asset data by path |
| `HasAssets` | `PackagePath`, `bRecursive` | Check if any assets exist under a path |
| `GetInMemoryAssets` | `Filter`, `OutAssetData` | Query only currently loaded assets |

### Dependency & Reference Queries

| Function | Key Params | Description |
|----------|------------|-------------|
| `GetDependencies` | `PackageName`, `DependencyOptions`, `OutDependencies` | Get packages this asset depends on |
| `GetReferencers` | `PackageName`, `ReferenceOptions`, `OutReferencers` | Get packages that reference this asset |

### Class Hierarchy

| Function | Key Params | Description |
|----------|------------|-------------|
| `GetAncestorClassNames` | `ClassPathName`, `OutAncestorClassNames` | Get parent classes |
| `GetDerivedClassNames` | `ClassNames`, `DerivedClassNames` | Get child classes |

### Path & Scanning

| Function | Key Params | Description |
|----------|------------|-------------|
| `GetAllCachedPaths` | `OutPathList` | Get all known content paths |
| `GetSubPaths` | `InBasePath`, `OutPathList`, `bInRecurse` | Get sub-paths under a base path |
| `ScanPathsSynchronous` | `InPaths`, `bForceRescan` | Force scan specific paths |
| `ScanFilesSynchronous` | `InFilePaths`, `bForceRescan` | Force scan specific files |
| `SearchAllAssets` | `bSynchronousSearch` | Trigger full asset scan |
| `ScanModifiedAssetFiles` | `InFilePaths` | Scan modified files |
| `IsLoadingAssets` | (none) | Check if scan is in progress |
| `WaitForCompletion` | (none) | Block until scan finishes |

### Filter & Sort

| Function | Key Params | Description |
|----------|------------|-------------|
| `RunAssetsThroughFilter` | `AssetDataList`, `Filter` | Filter asset list in-place (keep matching) |
| `UseFilterToExcludeAssets` | `AssetDataList`, `Filter` | Filter asset list in-place (remove matching) |

### UAssetRegistryHelpers (Static Utilities)

**CDO Path**: `/Script/AssetRegistry.Default__AssetRegistryHelpers`

| Function | Description |
|----------|-------------|
| `GetAssetRegistry` | Get the IAssetRegistry instance |
| `GetDerivedClassAssetData` | Get asset data for derived classes |
| `SortByAssetName` | Sort FAssetData array by name |
| `SortByPredicate` | Sort FAssetData array by custom predicate |

## Engine Built-in Asset Libraries

### IAssetTools — Create, Import, Export Assets

Get the instance via static helper:

```json
{"type":"call","object":"/Script/AssetTools.Default__AssetToolsHelpers","function":"GetAssetTools"}
```

Then call functions on the returned instance:

#### Create
| Function | Key Params | Description |
|----------|------------|-------------|
| `CreateAsset` | `AssetName`, `PackagePath`, `AssetClass`, `Factory` | Create a new asset (Factory can be null for simple types) |
| `CreateUniqueAssetName` | `InBasePackageName`, `InSuffix` | Generate a unique name to avoid conflicts |
| `DuplicateAsset` | `AssetName`, `PackagePath`, `OriginalObject` | Duplicate an existing asset |

#### Import & Export
| Function | Key Params | Description |
|----------|------------|-------------|
| `ImportAssetTasks` | `ImportTasks` (array of UAssetImportTask) | Import external files as assets |
| `ImportAssetsAutomated` | `ImportData` (UAutomatedAssetImportData) | Automated batch import |
| `ExportAssets` | `AssetsToExport`, `ExportPath` | Export assets to files |

#### Rename & Migrate
| Function | Key Params | Description |
|----------|------------|-------------|
| `RenameAssets` | `AssetsAndNames` (array of FAssetRenameData) | Batch rename/move assets |
| `FindSoftReferencesToObject` | `TargetObject` | Find soft references |
| `MigratePackages` | `PackageNamesToMigrate`, `DestinationPath` | Migrate packages to another project |

#### Example: Create a new material

```json
{"type":"call","object":"<asset_tools_instance>","function":"CreateAsset","params":{"AssetName":"M_NewMaterial","PackagePath":"/Game/Materials","AssetClass":"/Script/Engine.Material","Factory":null}}
```

### UEditorAssetLibrary — Asset CRUD

**CDO Path**: `/Script/EditorScriptingUtilities.Default__EditorAssetLibrary`

#### Query & Check
| Function | Description |
|----------|-------------|
| `DoesAssetExist(AssetPath)` | Check if an asset exists |
| `DoAssetsExist(AssetPaths)` | Batch existence check |
| `FindAssetData(AssetPath)` | Get asset metadata |
| `ListAssets(DirectoryPath, bRecursive, bIncludeFolder)` | List assets in directory |
| `LoadAsset(AssetPath)` | Load an asset into memory |

#### Create & Modify
| Function | Description |
|----------|-------------|
| `DuplicateAsset(SourceAssetPath, DestAssetPath)` | Duplicate an asset |
| `RenameAsset(SourceAssetPath, DestAssetPath)` | Rename/move an asset |
| `SaveAsset(AssetPath, bOnlyIfIsDirty)` | Save an asset to disk |
| `SaveLoadedAsset(LoadedAsset, bOnlyIfIsDirty)` | Save a loaded asset |
| `SaveDirectory(DirectoryPath, bOnlyIfIsDirty, bRecursive)` | Save all assets in directory |
| `SetMetadataTag(Object, Tag, Value)` | Set asset metadata tag |
| `GetMetadataTag(Object, Tag)` | Get asset metadata tag |
| `RemoveMetadataTag(Object, Tag)` | Remove asset metadata tag |

#### Delete
| Function | Description |
|----------|-------------|
| `DeleteAsset(AssetPath)` | Delete an asset |
| `DeleteLoadedAsset(AssetToDelete)` | Delete a loaded asset |
| `DeleteLoadedAssets(AssetsToDelete)` | Delete multiple loaded assets |

#### Directory Management
| Function | Description |
|----------|-------------|
| `MakeDirectory(DirectoryPath)` | Create a content directory |
| `DeleteDirectory(DirectoryPath)` | Delete a content directory |
| `DoesDirectoryExist(DirectoryPath)` | Check if directory exists |
| `DoesDirectoryHaveAssets(DirectoryPath, bRecursive)` | Check if directory contains assets |

#### Checkout (Source Control)
| Function | Description |
|----------|-------------|
| `CheckoutAsset(AssetToCheckout)` | Check out asset for editing |
| `CheckoutLoadedAsset(AssetToCheckout)` | Check out a loaded asset |
| `CheckoutDirectory(DirectoryPath, bRecursive)` | Check out all assets in directory |

### UEditorUtilityLibrary

**CDO Path**: `/Script/Blutility.Default__EditorUtilityLibrary`

| Function | Description |
|----------|-------------|
| `GetSelectedAssets()` | Get currently selected assets in content browser |
| `GetSelectedAssetData()` | Get selected asset data |

### UAssetEditorSubsystem

**CDO Path**: Use via `call` on the subsystem (get instance via `FindObjectInstances`)

| Function | Description |
|----------|-------------|
| `OpenEditorForAsset(Asset)` | Open asset editor |
| `CloseAllEditorsForAsset(Asset)` | Close all editors for asset |

## Common Patterns

### Browse content directory

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"ListAssets","params":{"DirectoryPath":"/Game/Materials","bRecursive":true,"bIncludeFolder":false}}
```

### Duplicate and rename an asset

```json
[
  {"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"DuplicateAsset","params":{"SourceAssetPath":"/Game/Materials/M_Base","DestinationAssetPath":"/Game/Materials/M_BaseCopy"}},
  {"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"RenameAsset","params":{"SourceAssetPath":"/Game/Materials/M_BaseCopy","DestinationAssetPath":"/Game/Materials/M_NewName"}}
]
```

### Delete an asset

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"DeleteAsset","params":{"AssetPathToDelete":"/Game/Materials/M_Unused"}}
```

### Query dependencies via AssetRegistry

First get the registry, then call GetDependencies on it:

```json
[
  {"type":"call","object":"/Script/AssetRegistry.Default__AssetRegistryHelpers","function":"GetAssetRegistry"},
  {"type":"call","object":"<returned_registry_path>","function":"GetDependencies","params":{"PackageName":"/Game/Materials/M_Example","DependencyOptions":{},"OutDependencies":[]}}
]
```

### Find all materials in project

```json
{"type":"call","object":"/Script/AssetRegistry.Default__AssetRegistryHelpers","function":"GetAssetRegistry"}
```
Then on the returned registry:
```json
{"type":"call","object":"<registry_path>","function":"GetAssetsByClass","params":{"ClassPathName":"/Script/Engine.Material","OutAssetData":[],"bSearchSubClasses":false}}
```

### Save all dirty assets in a directory

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"SaveDirectory","params":{"DirectoryPath":"/Game/Materials","bOnlyIfIsDirty":true,"bRecursive":true}}
```
