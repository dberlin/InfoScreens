# InfoScreens

**Version 0.0.4** · Mod reference: **`InfoScreens`** · [Source code](https://github.com/dberlin/InfoScreens)

Inventory and production information screens for Satisfactory, with support for single-player and multiplayer on Linux dedicated servers.

## Features

- Automatic inventory screens on storage containers and both sides of freight cars.
- One automatic screen on each solid or fluid freight platform, centered between its input/output ports.
- Placeable **Info Screen** and **Big Info Screen** for supported machines, inventories, power storage, and AWESOME Sink information.
- **Blueprint Info Screen** showing the combined configured input or output rates of a placed blueprint.
- Item icons, quantities, capacities, progress bars, and fluid volumes in m³.
- Adjustable screen placement and independent heading and item-list font sizes, with automatic fitting by default.
- Saved layouts, configuration, and reusable presets.

### New in 0.0.4

- Fixes freight-car screens disappearing in multiplayer.
- Positions freight-car screens against the car body and keeps them attached as trains move.
- Adds automatic screens to solid and fluid freight platforms.

## Requirements

- Satisfactory CL **513885** or a compatible newer build.
- Satisfactory Mod Loader **3.12.0** or a compatible 3.x version.
- Windows Steam or Epic Games client; Linux dedicated servers are supported.

For multiplayer, install the **same InfoScreens version on the server and every client**. Keep the mod installed when joining a server that uses it.

## Installation

### Satisfactory Mod Manager

Find **InfoScreens** in Satisfactory Mod Manager and install it for your game or server profile.

### Manual installation

1. Close the game or stop the server.
2. Choose the matching archive:
   - Windows: `InfoScreens-0.0.4-Windows-Install.zip`
   - Linux dedicated server: `InfoScreens-0.0.4-LinuxServer-Install.zip`
3. Extract it into the game or server installation folder containing **`FactoryGame`** and **`Engine`**. Allow existing InfoScreens files to be replaced when updating.
4. Start the game or server.

The resulting descriptor must be at `FactoryGame/Mods/InfoScreens/InfoScreens.uplugin`. Do **not** extract an install ZIP inside `Mods`; it already includes that directory structure. SML must be installed separately and is not replaced by these archives.

`InfoScreens-0.0.4.zip` is the combined package for ficsit.app, not the manual-install archive.

## Using screens

Storage containers, freight cars, and cargo platforms receive screens automatically, including those already present in a save. Empty train platforms and train stations do not receive cargo screens.

Find the placeable screens under **Organization → Signs** in the build menu. Place a normal or big Info Screen within 10 metres of a supported source; it selects the nearest supported source and stays associated with it until that source is removed.

Supported information includes:

- Storage and freight quantities, capacities, and mixed-inventory slot usage.
- Fluid volume and flow information.
- Manufacturing ingredients, products, and configured rates.
- Extractor output and generator fuel, power, and resource information.
- Battery charge and time estimates.
- Global AWESOME Sink progress and coupons.

Automatic screens cannot be dismantled separately; removing their source removes its screen. Placeable screens can be dismantled normally.

### Blueprint input and output totals

Place a **Blueprint Info Screen** against a building or foundation belonging to the desired blueprint. Press **E**, choose **Inputs** or **Outputs**, then **Apply**. Use two screens to show both at once.

The screen totals recipe rates across that specific blueprint instance. For example, four manufacturers each requiring 25 sulfur/min and 50 coal/min show:

```text
100 sulfur/min
200 coal/min
```

- Rates reflect configured recipes and clock speeds, not current throughput or inventory. Output amplification affects output totals only.
- Inputs and outputs are gross totals; ingredients produced elsewhere within the same blueprint are not subtracted.
- Recipe-based machines such as constructors, assemblers, smelters, foundries, manufacturers, and refineries are included. Extractors, generators, and transport buildings are not included in recipe totals.
- Each placed blueprint copy is tracked separately. Removing a machine removes its contribution without switching the screen to a neighboring blueprint.
- Screens can be saved inside Blueprint Designer templates.

If a screen reports no configured item rates, check that it is attached to the intended blueprint and that its production machines have recipes selected. Attaching a screen to an unrelated grouped foundation does not select a nearby factory.

### Layout and fonts

Press **E** on a screen to open its configuration. Adjust the front/back position, rotation, scale, and back-panel visibility, then choose **Apply**.

- Heading and item-list fonts have separate **Auto-fit** controls. Auto-fit chooses the largest size that fits horizontally and vertically.
- Disable Auto-fit to set a manual size from **1–256**. Oversized manual text can clip.
- **Save Preset** and **Load Preset** reuse layouts for automatic displays of the same source class.
- **Y/H**, **U/J**, and **T/G** move the selected panel's draft position by one centimetre. Choose **Apply** to save the adjustment.
- **Escape** closes the editor.

Freight cars have screens on both long sides. Cargo platforms have one screen on the input/output side. The large-side-panel option does not add extra panels to either.

## Chat commands

These settings apply to the shared game session.

| Command | Effect |
| --- | --- |
| `/modscreen ON` / `/modscreen OFF` | Enable or disable automatic screens on modded storage containers, freight wagons, and cargo platforms; vanilla sources remain enabled. |
| `/modscreen big` | Toggle additional large side panels on storage containers. |
| `/cstime <seconds>` | Set the update interval. Default: `10`; range: `0.1–3600`. |
| `/csdist <centimetres>` | Set how close a player must be for information to update. Default: `10000` (100 m); range: `100–1000000`. |

`/csdist` controls information updates, not screen visibility. Screens outside the update range can keep showing their last values.

## Credits

Inspired by [ContainerScreen](https://ficsit.app/mod/ContainerScreen). InfoScreens uses its own artwork and implementation.
