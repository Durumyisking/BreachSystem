# Niagara Examples Pack

- Publisher: Epic Games
- Source: https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600
- License displayed when acquired: Fab Standard License, https://www.fab.com/eula
- Downloaded UE asset version: 5.7
- Project location: `Content/NiagaraExamples`

Downloaded through Epic Games Launcher directly into BreachTest by selecting
Show All Projects and the pack's supported asset version. The project's source
engine association was preserved; no additional Unreal Engine was installed.

Wood impact system: `/Game/NiagaraExamples/FX_Weapons/Impacts/NS_Impact_Wood`
Concrete impact system: `/Game/NiagaraExamples/FX_Weapons/Impacts/NS_Impact_Concrete`

These assets have not been connected to BreachTest's damage or firing code.

## Validation with the existing UE 5.8.2 source build

- Installed files: NiagaraExamples 669; FreeSurfaceDecals5Pack 22.
- The four wood/concrete/glass/metal impact systems, all five decals, the decal
  demo map and their project asset dependencies loaded successfully: 113 assets,
  zero errors and zero warnings. The decal master has Deferred Decal domain.
- This was a headless load/dependency check, not a rendered or PIE effect test.
- Full-pack loading also reaches optional gallery and authoring utilities.
  `EUB_SnapToActor` requires DataprepEditor; the gallery reload montage requires
  MotionWarping; `Utilities/SpriteGeneration/ExplosionRoil` uses NiagaraFluids.
  These plugins are not enabled in BreachTest. A temporary validation run with
  them enabled failed to load the MotionWarping module. Its DLL exists, but its
  `UnrealEditor.modules` manifest is absent in the source engine installation.
  No engine files, project plugin settings, or imported assets were modified
  to bypass this issue. These optional samples are not verified usable.

Validation records: `Saved/ImpactAssetAudit.json` and
`Saved/Logs/ImpactAssetAudit.log`. The full-pack findings are in
`Saved/Logs/AssetImportAudit.log` and `Saved/Logs/AssetImportAuditPlugins.log`.
