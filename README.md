<p align="center">
  <img src="docs/banner.png" alt="Gathering Map: gathering spots on the quest map in Monster Hunter Generations Ultimate on MHGU Recomp" width="100%">
</p>

<p align="center">
  <a href="https://github.com/lick-mee/mhgu-gathering-map/releases/latest"><b>Download the latest release</b></a>
</p>

A map mod for [MHGU Recomp](https://github.com/morerokk/MHGURecomp), the PC port of Monster Hunter Generations Ultimate.

The quest map shows monsters, hunters and traps, but not where to gather. Gathering Map adds every gathering spot to the map, with the icon and color of the item it mostly gives: mining spots as ore, bone piles as bones, bug spots as bugs, plus herbs, mushrooms, honey, berries, webs, eggs, dung, whetstones, bait and wood. Icons sit under the game's own markers and only show for the areas the map is showing.

## Install

1. Download `gather-map-v1.0.0.zip` from [Releases](https://github.com/lick-mee/mhgu-gathering-map/releases).
2. Extract the `zzz-gather-map` folder into the `mods` folder of [MHGU Recomp](https://github.com/morerokk/MHGURecomp). Keep the name: it has to load after other mods.
3. Double-click `setup.bat` in that folder. It needs [Python 3](https://www.python.org/downloads/) (tick "Add python.exe to PATH" when installing) and ends with `map icons ready`.
4. Start the game.

The setup step adds the icons to the game's map icon textures. Game files can't be shipped, so it builds them from your own copy, including another mod's version if you use one (HD UI, Pixel Pack). **Run `setup.bat` again after updating such a mod.** To uninstall, delete the `zzz-gather-map` folder.

## Options

Press **Insert** in game and open **Gathering Map**:

- **Show gathering points on the map**: the mod itself.
- **Also show other pickups**: off by default. Crates, barrels and arena ammo spots, with the game's `!` dropped-item marker.
- **Icon size (%)**: 35 by default.

Options reset at each start.

## Compatibility

- Tested on MHGU Recomp 0.9.7, which runs the game at version 1.4.0, with the game in English.
- Works with UI and texture mods ([Pixel Pack v4](https://www.reddit.com/r/MonsterHunter/comments/1icfs4z/mhgu_hd_texture_mod_60_fps_and_multiplayer_2025/)'s HD UI, [HD Hunter Font](https://github.com/lick-mee/mhgu-hd-hunter-font), [Confirm on X](https://github.com/lick-mee/mhgu-confirm-on-x), …) as long as `setup.bat` runs after they are installed.
- Spots show where they can be. Whether one is up on this particular quest, or already gathered, isn't tracked.
- Recomp only: it's a Forge plugin that hooks the recomp's game code. Emulators aren't supported.

## How it works

Each map has a set of gathering points per area (world position, kind of spot) and item lots per rank; a point's icon comes from the items its lot gives most, using the icons and colors of the game's own item table. `tools/make_data.py --header` turns them into `src/points.h`.

The quest map draws its icons through one icon object per map view. Each keeps a GUI instance of the `map_icon` layout per icon and every frame plays a sequence on it (color), converts a world position to the map and shows or hides it. The plugin hooks that object: when it initializes, the plugin makes extra instances of its own, and after each of its updates places them on the gathering points of the current map, setting the icon's texture cut directly. `tools/make_data.py` (the release's `setup.bat`) copies the item icons into free space of the map icon texture and adds one sequence per icon and color to `map_icon`, in your `quest_eng.arc`. `src/plugin.c` has the game addresses and offsets.

## Thanks

[MHGU Recomp](https://github.com/morerokk/MHGURecomp), which this mod is made for, and [mhw-gui-editor](https://github.com/Fexty12573/mhw-gui-editor) for the GUI file layout.

## License

MIT for the code. Monster Hunter Generations Ultimate © CAPCOM.
