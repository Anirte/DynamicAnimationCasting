# Dynamic Animation Casting NG — 1.7.104 update (unofficial)

Unofficial fork of [ArcEarth's Dynamic Animation Casting - NG](https://github.com/ArcEarth/DynamicAnimationCasting),
itself based on [Loki's Dynamic Animation Casting](https://www.nexusmods.com/skyrimspecialedition/mods/65512).

An SKSE plugin that casts spells on animation events, driven by `.toml` files. See the
[original mod page](https://www.nexusmods.com/skyrimspecialedition/mods/73293) for the configuration format.

## What this fork changes

- **Skyrim SE 1.7.104 support.** Built against CommonLibSSE-NG v10.1.0, which reads the new Address Library
  format. No relocation IDs were changed.
- **HUD bar flashing fix.** The original checked the health/stamina/magicka cost first and flashed the HUD bar
  immediately. The player's bars therefore flashed when an NPC could not afford a trigger, and for triggers whose
  other conditions (weapon, perk, effect, keyword, ...) were not met. A bar now flashes only for the player, and
  only after every other condition of the trigger has passed. Costs and cast logic are unchanged.
- API updates for current CommonLibSSE-NG: `RestoreActorValue(modifier, ...)` → `ModActorValue`,
  `RE::DebugNotification` → `RE::SendHUDMessage::ShowHUDMessage`, no `<Windows.h>` before CommonLib headers.
- The include of `Expression.h` was removed: that file was never published and nothing from it is used.
- VR is not built.

## Building

Requirements: Visual Studio 2022 or 2026 (or Build Tools) with *Desktop development with C++* and its vcpkg
component.

    build.bat

The result is `build\loki_DynamicAnimationCasting.dll`. CommonLibSSE-NG v10.1.0 is downloaded and compiled as
part of the build; set `COMMONLIB_DIR` to an existing checkout to skip the download.

The binary contains only the plugin DLL. The Papyrus script, `template.toml` and `AnimEvents.txt` come from the
original mod.

## In-game self test

Configure with `-DDAC_AUTOTEST=ON` to build a test variant of the plugin. It adds a console command,
`DynAnimTest`, which teleports the player to QASmoke, spawns three wolves and runs five timed phases:

1. NPCs lack magicka for a trigger - the HUD bar must not flash
2. the player lacks magicka - the bar must flash
3. a player-only trigger for another weapon type - the bar must not flash
4. a realistic trigger (`weaponSwing`, one-handed sword, Firebolt) with enough magicka - spells are cast, no flash
5. the same trigger with no magicka - no cast, the bar flashes

Each phase logs a PASS/FAIL verdict to `DynamicAnimationCasting.log`, together with how often the original code
would have flashed the bar and how often this build really did. The test code is compiled out of normal builds.
Never distribute the test variant.

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).

This fork links against [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) (GPL-3.0-or-later), which
makes the resulting binary a combined work under the GPL.

The original code is MIT licensed, Copyright (c) 2022 Loki. That license and copyright notice are preserved in
[LICENSE-MIT-ORIGINAL](LICENSE-MIT-ORIGINAL).

## Credits

- Loki — the original Dynamic Animation Casting
- ArcEarth — Dynamic Animation Casting - NG
- hsoju — Cooldown and CustomSpell features
- D7ry — AE address of the hook
- Ershin — AE hook address (TrueHUD) and the weapon tip node (Precision)
- alandtse, CharmedBaryon and contributors — CommonLibSSE-NG
