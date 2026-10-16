# InfoScreens

Source version **0.0.2** · mod reference **`InfoScreens`** · [source repository](https://github.com/dberlin/InfoScreens)

A native Satisfactory mod implementing ContainerScreen-style automatic container displays and standalone machine information screens, with server-authoritative telemetry for multiplayer.

**Release 0.0.1: user-verified working in multiplayer**, in addition to the single-player and Windows-client/Linux-server checks below on CL 513885. Its container/machine/blueprint packages are preserved separately from **0.0.2**, which adds freight cars. Builds now go to `dist/<SemVersion>/`; the existing unversioned `dist/` and Dropbox ZIPs remain 0.0.1. This is not a claim of pixel-identical visuals or exhaustive production-chain coverage.

## Behavior implemented in source

- Automatic front/back displays on standard storage containers; optional modded-container support and large side panels.
- Buildable **Info Screen** and **Big Info Screen**, unlocked automatically under Organization / Signs. Normal screens cost two reinforced iron plates; big screens are free, matching the inspected reference assets.
- Storage quantities/capacity, mixed-inventory slot usage, fluid volume and flows, manufacturing ingredients/products/rates, extractor output, generator fuel/power/supplemental resources/nuclear waste, battery charge and time estimates, and global AWESOME Sink progress/coupons.
- Item icons, progress bars, and numeric readouts. Dedicated servers create no UMG widgets or render targets.
- E opens a layout editor: front/back position, rotation and scale, back-panel visibility, Apply, Save Preset, Load Preset, and Close. Presets apply to automatic displays of the same container class. Y/H, U/J and T/G adjust the selected panel's draft position by one centimetre; Apply submits it. Escape closes through the game's interaction UI.
- Layouts, presets and configuration persist in saves. Automatic displays are recreated from their sources and cannot be independently dismantled or refunded. Standalone screens use normal building/dismantling behavior.

Chat commands:

| Command | Meaning |
| --- | --- |
| `/modscreen ON` / `/modscreen OFF` | Enable/disable automatic displays on modded storage containers and freight wagons |
| `/modscreen big` | Toggle large automatic side panels |
| `/cstime <seconds>` | Sampling interval; default 10, accepted range 0.1–3600 |
| `/csdist <centimetres>` | Player proximity required for updates; default 10000, accepted range 100–1000000 |

`/csdist` controls updates, not display visibility. An out-of-range display can retain its last snapshot.

### Train freight cars

- Vanilla freight cars receive automatic displays on both long sides, attached to the wagon rather than left at their original world position. Existing saved wagons and newly built wagons are discovered.
- Solid cargo uses the same item-count/capacity display as containers; mixed cargo shows occupied slots. Fluid and gas cargo show volume/capacity in m³. An empty wagon whose game cargo type is unspecified shows empty slots rather than guessing its next cargo.
- Uses the existing server-authoritative sampling, proximity settings, layout editor and saved class presets. Removing a wagon removes its automatic screen. Standalone Info Screens can also select nearby freight cars.
- Freight's primary panels already occupy both sides, so `/modscreen big` does not add panels over the couplers. Modded freight classes follow the existing modded-container toggle.

### Blueprint Info Screen

A separate, manually placed buildable under **Organization → Signs**, costing two reinforced iron plates. Place it against a building or foundation belonging to a placed blueprint. Press **E**, select **Inputs** or **Outputs**, then **Apply**.

- Aggregates configured recipe rates by item descriptor across that exact blueprint instance, not a nearby area or every copy of the blueprint.
- Four machines consuming 25 sulfur/min and 50 coal/min each produce two input rows: **100 sulfur/min** and **200 coal/min**. Ingredient positions and recipe differences do not split matching items into separate rows.
- Uses configured clock speed and manufacturing speed, independent of current inventory, power, standby state or measured productivity. Output amplification affects output totals only. Fluids and gases display m³/min.
- Shows gross inputs or gross outputs; internal production/consumption is not netted. Covers recipe-based production machines, including constructors, assemblers, manufacturers and refineries. Resource extractors and fuel generators are not recipe-based and are not included in these totals.
- Saves the selected mode and blueprint binding. Removing the originally selected building retains the surviving blueprint's totals; removing machines removes their contribution. It never silently switches to a neighboring blueprint.
- Can be included inside a Blueprint Designer template. Instantiated screens use their own new instance's membership rather than an external template reference.
- Sampling uses the existing `/cstime` and `/csdist` settings. Mode changes apply immediately. Multiple ingredient rows scale down to remain within the display.


### Multiplayer ownership

The server alone reads source inventories and machinery. Screen actors replicate display snapshots and layouts; clients render these snapshots rather than reading conditionally replicated inventories. Sampling is shared per source. Player-owned remote-call objects validate editor target, distance, and layout values on the server. Revision tracking preserves newer unsent drafts; request tokens reject replies from closed or replaced editors.

Displays opt out of the game's static factory-proximity index. Attached automatic displays can change position as replication resolves their source; indexing them as stationary factory buildings caused a client crash when the source was removed. Their source buildings remain responsible for factory-area proximity.

The plugin is required on both server and clients. A client-only or server-only installation is not supported by its descriptor.

### Deliberate choices where reference behavior was unclear

Normal and Big Info Screens select a supported source whose collision bounds are within 1000 cm, choosing nearest bounds, then nearest center, then stable actor path for ties. A valid association is retained until the source disappears. Discovery retries when no valid source exists. The original overlap radius and arbitration were not recoverable with confidence. Blueprint Info Screens instead retain an exact blueprint-instance reference.

Automatic unlock and build-menu categorization are implementation choices. Sink displays report the game's global sink tracks, not invented per-building throughput. This project uses newly generated artwork rather than repackaged reference assets. Exact visual parity has not been verified.

## Current toolchain

| Component | Target |
| --- | --- |
| Game / headers | CL 513885 |
| Custom engine | UE 5.6.1-CSS, engine CL 83 |
| SML | 3.12.0; descriptor accepts >=3.12.0 <3.13.0 |
| SDK source | [8eebc9e659f0b2ff4392d2f8cf6e2cff60c8ea49](https://github.com/satisfactorymodding/SatisfactoryModLoader/tree/8eebc9e659f0b2ff4392d2f8cf6e2cff60c8ea49) |
| Wwise SDK | 2025.1.6.9117 |
| Wwise integrated plugin | 2025.1.6.9117.4248 |
| CLI integration version | 2025.1.6.4248 |
| Package targets | Windows Steam/EGS clients and Linux dedicated server |

The updated SDK is isolated at `~/satisfactory-modding/info-screens-workspace/sdk`. The older shared starter and runtimes were not upgraded in place. The installed custom engine remains the required current engine; replacing it with stock Unreal is not supported.

### Install Wwise and build

Wwise 2025.1.6 is installed in the isolated SDK. Its SDK headers and libraries live under `Plugins/WwiseSoundEngine/ThirdParty`, not the older `Plugins/Wwise/ThirdParty` location. `~/satisfactory-modding/install-wwise.sh` now runs the commands below for this workspace. For a fresh install, authenticate locally through the CLI; do not put passwords or tokens in source files or chat.

```sh
~/satisfactory-modding/bin/wwise-cli \
  --cache-dir ~/satisfactory-modding/info-screens-workspace/wwise-cache \
  download --sdk-version 2025.1.6.9117 \
  --filter Packages=SDK \
  --filter DeploymentPlatforms=Windows_vc160 \
  --filter DeploymentPlatforms=Windows_vc170 \
  --filter DeploymentPlatforms=Linux \
  --filter DeploymentPlatforms= \
  --filter Packages=Authoring

~/satisfactory-modding/bin/wwise-cli \
  --cache-dir ~/satisfactory-modding/info-screens-workspace/wwise-cache \
  integrate-ue --integration-version 2025.1.6.4248 \
  --project ~/satisfactory-modding/info-screens-workspace/sdk/FactoryGame.uproject
```

Then, from this repository:

```sh
source ~/satisfactory-modding/activate.sh
python Tools/package_mod.py --preflight
python Tools/package_mod.py
```

`--preflight` does not mutate the SDK. Packaging stages only this plugin, builds the Linux editor target, imports the original assets, builds the shipping targets, then invokes Alpakit to cook and package Windows clients and Linux servers. It validates native manifests, binaries and cooked content before copying archives to `dist/<SemVersion>/` (currently `dist/0.0.2/`). Detailed logs go to `build/`. Use `--output` to select a different destination.

Do not run another build, cook or integration operation against the same SDK or build Wine prefix concurrently. The workflow refuses its existing locks and unrelated staged plugins; it does not steal ownership.

UnrealBuildTool serializes builds across projects sharing this engine. The script passes `-WaitMutex` directly to UBT; a quiet build may be waiting for another project's build, not compiling. UAT's `-ubtargs=-WaitMutex` embeds the flag inside target arguments and does not enable the global wait, so shipping compilation is performed separately before Alpakit's `-skipbuild` cook/package step.

The installed `msvc/bin/x64/wine-msvc.sh` required a response-file fix: preserve original line boundaries while translating paths. Flattening the whole file caused MSVC LNK1170 above 131070 characters; splitting every argument onto its own line broke CL's separate `/I` argument parsing. Both compiler and linker succeeded with original line boundaries preserved.

After a successful package, individual platform ZIP contents belong in `FactoryGame/Mods/InfoScreens/`, with `InfoScreens.uplugin` directly inside that directory. The merged ZIP has a different platform-directory layout.

For Windows installation, use **`InfoScreens-Windows-Install.zip` from the desired version directory**: close the game and extract into the Satisfactory installation folder containing `FactoryGame` and `Engine`. The ZIP already contains `FactoryGame/Mods/InfoScreens/`. It includes both Steam and EGS binaries and does not include or replace SML. Do not extract this install ZIP inside `Mods`, or its directory prefix will be duplicated.

For Linux dedicated servers, extract **`InfoScreens-LinuxServer-Install.zip` from the same version directory** into the server installation folder containing `FactoryGame` and `Engine`. Both install archives preserve the platform package's file permissions and leave SML untouched. Use the same mod version on clients and server; the client installation stays installed when joining a server running this mod.

For ficsit.app, use **`InfoScreens.zip` from the desired version directory**, the merged Alpakit archive containing `Windows/` and `LinuxServer/`. The `-Install.zip` archives are for manual installation, not uploading. Dropbox filenames include the version, such as `InfoScreens-0.0.1.zip`, to keep releases distinct.

### Upload to ficsit.app

Create the listing on [ficsit.app](https://ficsit.app) with the exact mod reference **`InfoScreens`**, complete its AI-use and network-use disclosures, and obtain listing approval. Link the source repository above. This project was AI-assisted; the mod uses Unreal's multiplayer replication and does not implement external HTTP requests.

Sign in through the website's OAuth flow. The uploader needs the **SMR session token** (the signed-in site's `token` cookie), **not** a GitHub PAT, password or provider access token. Keep it private; supply it through the hidden prompt or `FICSIT_TOKEN` in your environment, never a command-line argument or committed file.

```sh
python3 Tools/upload_ficsit.py \
  --archive dist/0.0.1/InfoScreens.zip \
  --mod-reference InfoScreens \
  --changelog "Initial 0.0.1 release" \
  --stability release
```

Use `--mod-id LISTING_ID` instead of `--mod-reference`, or `--changelog-file /path/to/notes.txt` instead of inline notes. The script validates matching release version metadata from the selected archive (including both 0.0.1 and 0.0.2), checks account/listing access, uploads numbered chunks unchanged, finalizes, and waits for processing. Successful processing can still require scanning/moderator approval. It does not create listings or bypass approval.

This command performs a real upload. If finalization or polling fails, inspect the listing before rerunning: server processing may continue after the client exits. There is no automatic retry or destructive cleanup. Credentials have not been supplied here, so no production release has been uploaded.

Verification: 21 uploader tests passed against a localhost HTTP fixture, including exact multipart reconstruction, asynchronous processing, authentication failures, redirect refusal and token redaction. The CLI accepted the real merged package; a live read-only identity query with an invalid session was correctly rejected. No authenticated production upload was performed.

Protocol references: [official uploader](https://github.com/satisfactorymodding/smr-frontend/blob/staging/src/lib/utils/chunked-upload.ts), [authentication](https://github.com/satisfactorymodding/smr-frontend/blob/staging/src/lib/core/graphql.ts), and [API implementation](https://github.com/satisfactorymodding/smr-api/blob/staging/gql/resolver_versions.go).

### Client prerequisite

The current licensed Windows client is at `~/Satisfactory`; its Steam native-module manifest identifies CL 513885. Runtime verification uses an isolated copy and separate user data rather than modifying that installation. The older isolated CL 502094 client is preserved and is not compatibility evidence.

## Verification performed

- Configured and compiled the host C++ state tests with GCC 16.2.1; CTest passed `multiplayer_edit_ordering` (one executable covering eight state-ordering scenarios). These exercise the production draft/request state classes, not Unreal networking.
- Ran the custom engine's `UnrealEditor-Cmd` Python asset importer in an isolated, engine-only content-authoring project. It exited 0 with 0 errors and 0 warnings, producing the frame mesh, frame material and icon `.uasset` files in this plugin. This was asset authoring, not a substitute SDK or a mod gameplay launch.
- Installed the current anonymous Linux dedicated-server runtime in `~/satisfactory-modding/info-screens-workspace/linux-server`, installed official SML 3.12.0, and launched it with isolated user data, loopback binding, unique ports and an immediate quit command. It exited 0 and logged game CL 513885 / SML 3.12.0. **InfoScreens was not installed in that run.**
- Packaging preflight passes with the installed Wwise 2025.1.6 integration. The updated installer passes `bash -n`; it was not rerun over the completed installation.
- Checked Python script syntax and regenerated original source art.
- Built the native Linux editor module successfully against the current SDK. Reimported assets with the native module loaded: exit 0, 0 errors, 18 warnings. The importer updates existing material nodes instead of deleting nodes rooted by native class defaults.
- Ran all five `InfoScreens.*` Unreal automation tests: the JSON report records 5 successes, 0 failures, 0 warnings. The editor process nevertheless exited 1 because of SDK/engine startup smoke-test errors (uninitialized FactoryGame/SML structs and relative-path assertions), and missing generated Wwise metadata. This is not a clean whole-editor test run.
- Launched an isolated copy of the current Steam client with SML. NullRHI startup exited 0. DXVK 3.1.1 enabled actual D3D11 menu rendering on the host's llvmpipe renderer after native Vulkan and Wine's built-in D3D11 failed. The Server Manager completed an authenticated local-server join, but the subsequent gameplay connection timed out after severe frame stalls. This was a baseline run without InfoScreens and does not establish sustained multiplayer behavior.
- Built both Windows shipping targets (Steam and EGS) and the Linux dedicated-server target, then cooked and packaged through Alpakit. Validated each platform archive's CL 513885 descriptor, remote requirement, SML native module manifests, native binaries and cooked content; copied both platform ZIPs and the merged ZIP to `dist/`. Logs: `build/shipping.log` and `build/package.log`.
- Installed the packaged mod in the isolated Linux server. Startup loaded its native module and cooked assets, registered its subsystem/commands, and exited 0. A separate throwaway native probe then exercised real gameplay actors: fluid buffer, constructor, miner, coal generator, battery and sink readers returned their expected types. A real container seeded with 37 iron plates produced an automatic screen showing 37 with capacity, rejected dismantling, accepted a layout change, and created no widget components on the dedicated server. The authority probe completed with zero failures. This covers idle machine reads and populated storage, not running production chains or client replication. Evidence: `linux-server/infoscreens-startup.log` and `linux-server/infoscreens-probe.log` beneath the workspace above.
- Joined the real Linux server from the isolated Steam client with InfoScreens installed on both. With fixtures beside the actual player and normal screen relevance, the client received the 37-plate snapshot and the server-edited layout; direct client inventory reading was denied. A full client restart/rejoin received the same existing state. Both client probes completed with zero failures. The NullRHI connection sustained gameplay, and the subsequent D3D11 run also passed these replication checks. Evidence: `windows-client/infoscreens-nearby-client.log`, `windows-client/infoscreens-reconnect-client.log`, `windows-client/infoscreens-visual-client.log`, and `linux-server/infoscreens-nearby-server.log`. These are real network checks, not mocks; visual/UI acceptance remains separate.
- Saved the isolated server scenario as `InfoScreensPersistenceVerification`, reloaded it without restarting the probe or respawning fixtures, and rejoined from a fresh client process. The automatic screen was recreated, retained its edited layout, and replicated the saved 37-plate inventory. Evidence: `windows-client/infoscreens-after-reload-client.log`.
- Created a real local single-player session through the game's save/session API and repeated the seven source-type checks, populated-storage sampling, layout application and automatic-display dismantle protection. The single-player authority probe completed with zero failures. Evidence: `windows-client/infoscreens-single-player.log`.
- Rendered the actual automatic display using D3D11/DXVK: item icon, Storage Container title, Iron Plate row, 37 / 4800 capacity and 0.77% fullness were visible. Opened the native editor with E, rejected an out-of-range edit, then moved within range and applied a keyboard-adjusted layout through the real UI. Server inspection confirmed front X changed from 308 to 309 cm. Save Preset captured a 310 cm draft; after applying 311 cm, Load Preset restored 310 cm. Escape closed the editor. Screenshots: `windows-client/infoscreens-display-settled.png`, `windows-client/infoscreens-editor-visual.png`, `windows-client/infoscreens-focused-apply.png` and `windows-client/infoscreens-accepted-edit.png`. A vanilla welcome overlay obscured part of the view.
- Submitted actual multiplayer chat commands for large side panels, a 2-second interval and a 12000 cm activation distance; server state reflected each change. An invalid zero interval was rejected without changing the configured interval.
- Repeated a real multiplayer lifecycle scenario with all seven source categories and both standalone screen sizes. Verified nearest-source association, retention of a valid source, shared preset inheritance by a newly spawned container, automatic-display removal when its container disappears, and reassociation of both standalone sizes to a replacement containing 37 plates. All seven snapshot kinds reached the Windows client; dedicated-server screens had no widget components. The scenario initially caught the proximity-index crash described above; the corrected packages completed on both server and client with zero failures. Evidence: `linux-server/infoscreens-lifecycle-fixed-server.log` and `windows-client/infoscreens-lifecycle-fixed-client.log`.
- Repeated that lifecycle scenario in a real local single-player session: zero failures. Rendered the Big Info Screen with its reassociated 37 / 4800 inventory and 0.77% fullness. Evidence: `windows-client/infoscreens-lifecycle-single-player.log` and `windows-client/infoscreens-big-single-player-settled.png`; the same vanilla welcome overlay partly obscured the view.
- Rebuilt and validated the final Windows Steam/EGS and Linux-server packages after the lifecycle fix. ZIP integrity checks passed and no runtime-probe module is included. The host state suite passed again.
- Added three native blueprint-rate regression cases: the four-machine 100/200 example, output selection, configured speed/clock/amplification, mixed recipes, fluid/gas units and invalid/empty membership. All eight `InfoScreens.*` cases passed with zero test failures or warnings; the whole editor still exited 1 due to the previously documented SDK startup issues. Evidence: `build/blueprint-automation/index.json` and `build/blueprint-automation.log`.
- Installed the final game-root Windows ZIP into the isolated current Steam runtime and confirmed the existing SML descriptor was unchanged. A real single-player probe constructed the new screen through its recipe hologram against an actual blueprint member. Four real Iron Plate constructors produced 120 ingots/min inputs and 80 plates/min outputs; an adjacent four-machine blueprint was excluded. Installing power shards and an amplifier verified configured clock and output-only amplification while idle. Removing the original anchor retained the group; removed machines no longer contributed. Template serialization cleared/restored external references, and a screen assigned to a copied instance preferred its own membership. The final scenario completed with zero failures. Evidence: `windows-client/infoscreens-blueprint-final-smoke.log`.
- Reloaded the saved blueprint screen from a fresh game process: output mode, exact proxy binding and the remaining 40 plates/min were restored. In the actual E editor, selected Inputs and clicked Apply; the authority accepted the change and the world screen displayed 60 ingots/min. Final visuals confirmed separated item/rate columns. Evidence: `windows-client/infoscreens-blueprint-final-visual.log`, `windows-client/infoscreens-blueprint-final-applied.png` and `windows-client/infoscreens-blueprint-final-input.png`. Blueprint Designer serialization hooks and copied-instance precedence were exercised, but creating and placing a complete designer blueprint file through the game's UI was not exercised.
- The user independently verified that **0.0.1 works in multiplayer**. That confirmation does not claim unreported individual blueprint placement paths or freight support; freight is a separate 0.0.2 change.
- All nine native `InfoScreens.*` tests passed after freight support, with zero test warnings/failures and the same whole-editor startup caveat. The host state test and 21 uploader tests passed. Evidence: `build/freight-automation/index.json`.
- A real single-player freight fixture displayed 37 / 6400 iron plates, followed wagon translation/rotation, then displayed 123 / 2400 m³ water after configuring the game's liquid cargo inventory. Unloading cleared the amount; deleting the wagon removed its screen. Save/reload restored cargo, edited layout and recreated attachment. Evidence: `windows-client/infoscreens-freight-cargo-configured.log` and `windows-client/infoscreens-freight-reload.log`. The earlier fluid fixture inserted into a solid-mode inventory before its resize and lost excess stacks; the display correctly reported the actual retained 50 m³. Configuring cargo type before transfer corrected the fixture without changing production code.

Evidence includes `build/assets-fixed.log`, `build/automation.log`, `build/automation/index.json`, `build/host/Testing/Temporary/LastTest.log`, and baseline runtime logs/screenshots under `~/satisfactory-modding/info-screens-workspace/{linux-server,windows-client,asset-authoring}`.

Re-run host tests:

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Native automation tests are under `Mods/InfoScreens/Source/InfoScreens/Private/Tests`; all eight selected cases passed with the whole-editor caveat above. Verification limits: existing machine fixtures exercised idle typed telemetry, not complete running production chains; concurrent editing was checked through production state-machine tests rather than two simultaneous game clients; modded-container commands were exercised without installing a third-party container mod. Original visual checks were partly obscured by the vanilla welcome overlay. The EGS target was compiled and packaged, not runtime-tested. The new blueprint screen is being delivered for Windows single-player testing first; its new remote-construction and lightweight-foundation paths have not yet had dedicated-server runtime acceptance. Temporary native probes are confined to isolated test installations and never shipped.

## Sources

- [ContainerScreen listing](https://ficsit.app/mod/ContainerScreen)
- [Inspected ContainerScreen asset revision](https://github.com/DellAquila/ContainerScreen/tree/95cab0093f3220cc0e44f96b9d578e213ae208d9)
- Local installed-toolchain handoff: `~/modding-sf.md` (older starter/runtime versions; not the updated SDK's compatibility authority).
