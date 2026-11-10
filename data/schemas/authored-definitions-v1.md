# Authored definition formats v1

These are strict UTF-8 key/value formats. Each file starts with its exact type/version header, followed by unique `key = value` records. Blank lines and lines beginning with `#` are ignored. Unknown fields, duplicate keys, missing required values, wrong type/version headers, manifest identity mismatches, invalid references, and undeclared dependencies are errors. Failed loads do not publish partial output.

## Faction — `.tfaction`

Header: `talvisota-faction<TAB>1`

Required: `id`, `name`, `side`.

`side` is one of `finnish`, `soviet`, or `neutral`. Optional: `description`.

## Character — `.tcharacter`

Header: `talvisota-character<TAB>1`

Required: `id`, `name`, `faction_id`.

Optional: `model_id`, `portrait_texture_id`, `description`.

Faction references must resolve to a manifest-registered `.tfaction`; model and texture references must resolve to the corresponding manifest asset kinds.

## Equipment — `.tequipment`

Header: `talvisota-equipment<TAB>1`

Required: `id`, `name`, `category`.

`category` is one of `clothing`, `tool`, `weapon`, `medical`, or `other`. Optional: `model_id`, `icon_texture_id`, `description`.

## Mission — `.tmission`

Header: `talvisota-mission<TAB>1`

Required: `id`, `name`, `world_id`, `player_character_id`.

Required: `region_id` in addition to the fields above. Optional: `faction_id`, `description`, and `next_mission_id`. Ordered objectives use repeated `objective = objective-id -> placement-id` records. World, region, character, faction, and optional successor references must resolve to `.tworld`, `.tregion`, `.tcharacter`, `.tfaction`, and `.tmission` assets respectively; every reference must be declared as a manifest dependency. A missing `next_mission_id` ends the authored campaign sequence.

## Reference policy

Every non-empty reference must exist in the manifest, resolve to the expected asset kind/type, and appear in the owning asset's dependency list. Referenced files must be regular files. The v1 parser rejects unsupported versions rather than guessing at forward compatibility.
