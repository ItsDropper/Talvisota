# World

Talvisota owns world identity, region composition, spatial metadata, collision authoring, navigation graph authoring, and region lifecycle. VantaEngine owns generic asset import, GPU resources, and rendering.

## World region v1

A .tworld defines the global metre-based coordinate space and bounds. A .tregion defines a bounded region inside that world, including an optional sampled terrain heightfield. Region files reference visual model/material assets through stable manifest IDs, but collision boxes and navigation nodes/edges are authored as independent game-owned records; neither is inferred from visual geometry.

The runtime loader validates region identity, parent-world identity, nested bounds, record limits, unique IDs, finite transforms, positive scales/extents, asset kinds, asset presence, explicit manifest dependencies, and navigation references. Terrain heightfields use increasing-X columns and increasing-Z rows, metre spacing, bilinear elevation interpolation, and gradient-derived surface normals. Queries outside the sampled grid fail without modifying outputs. Placement transforms use the documented column-major world convention. Collision AABB queries operate on the independent authored collision boxes. Directed navigation uses deterministic Dijkstra shortest paths over authored edges. Loading and reloading publish a new region only after all checks succeed. A failed reload leaves the previous region active; unload clears it.

The first authored region is Central Finland at content/worlds/central-finland.tregion. Its 17x17 heightfield is sampled from the same 4-metre terrain grid as the source OBJ, keeping terrain traversal height aligned with the authored visual mesh at grid vertices. The terrain remains a low-detail pipeline fixture, not the final environment.

Build and run the headless region lifecycle tests through CTest. Renderer integration remains subject to the M2 real-device gate in the development plan.
