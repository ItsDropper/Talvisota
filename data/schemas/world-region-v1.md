# World region v1

World-region source files use UTF-8 .tregion text. The first non-empty, non-comment line is:

    talvisota-region<TAB>1

Every record is tab-separated. The region asset itself must be registered as a definition in the content manifest. Its manifest dependencies must include the parent .tworld, every placed model, and every explicitly assigned .tmat material.

## Required singleton records

| Record | Fields | Contract |
|---|---|---|
| id | stable region asset ID | Must identify this exact registered .tregion file. |
| world | stable world ID | Must match the loaded parent world and be an explicit manifest dependency. |
| bounds_min | X,Y,Z | Finite metre coordinates. |
| bounds_max | X,Y,Z | Finite metre coordinates, strictly greater than the minimum and contained by parent-world bounds. |

## Repeated visual placement records

    placement<TAB>id<TAB>category<TAB>model_id<TAB>material_id-or--<TAB>x<TAB>y<TAB>z<TAB>yaw_degrees<TAB>uniform_scale

Categories are terrain, vegetation, rock, structure, and prop. Placement IDs are unique within the region. Model references must resolve to present model assets and be explicit region dependencies. A non-dash material reference must resolve to a present material asset and also be an explicit dependency. Position and yaw must be finite; uniform scale must be finite and positive; placement origins must lie within region bounds. Model import and renderer submission remain separate responsibilities.

## Collision records

    collision<TAB>id<TAB>center_x<TAB>center_y<TAB>center_z<TAB>half_x<TAB>half_y<TAB>half_z

Collision boxes are game-owned axis-aligned volumes, independent of visual model geometry. IDs are unique across all region records. Centers must be finite, half-extents must be positive, and the complete box must fit within region bounds. Runtime collision queries will consume this data without depending on renderer meshes.

## Navigation records

    nav_node<TAB>id<TAB>x<TAB>y<TAB>z
    nav_edge<TAB>from_node_id<TAB>to_node_id

Node IDs are unique across all region records. Node positions must be finite and inside region bounds. Edges are directed, must connect two distinct declared nodes, and may not be duplicated. The format deliberately does not infer navigability from render geometry or collision meshes.

## Safety limits and failure behavior

The v1 loader rejects files larger than 16 MiB, lines longer than 4096 bytes, more than 200000 records, unknown record types, malformed fields, non-finite values, invalid bounds, unresolved assets, missing manifest dependencies, and invalid navigation references. Failed loads do not modify the caller's previous output. Region-session reload is transactional: a failed replacement preserves the previously active region; unload clears the active region.

The current runtime implementation loads and validates authored composition and spatial metadata, builds placement transforms, executes axis-aligned collision overlap queries, and finds deterministic shortest paths across directed navigation edges. It does not yet claim renderer-backed presentation, asynchronous region streaming, or measured large-world memory/performance behavior. Those remain distinct M4 integrations with their own tests and measured need.