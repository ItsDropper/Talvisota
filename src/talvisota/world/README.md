# World

Talvisota owns world identity, region composition, spatial metadata, collision authoring, navigation graph authoring, and region lifecycle. VantaEngine owns generic asset import, GPU resources, and rendering.

## World region v1

A .tworld defines the global metre-based coordinate space and bounds. A .tregion defines a bounded region inside that world. Region files reference visual model/material assets through stable manifest IDs, but collision boxes and navigation nodes/edges are authored as independent game-owned records; neither is inferred from visual geometry.

The runtime loader validates region identity, parent-world identity, nested bounds, record limits, unique IDs, finite transforms, positive scales/extents, asset kinds, asset presence, explicit manifest dependencies, and navigation references. Loading and reloading publish a new region only after all checks succeed. A failed reload leaves the previous region active; unload clears it.

The first authored region is Central Finland at content/worlds/central-finland.tregion. Its current terrain placement and simple trail graph exercise the data path; this is not yet a final environment, a collision-query implementation, a pathfinder, a streaming system, or renderer-backed presentation.

Build and run the headless region lifecycle tests through CTest. Renderer integration remains subject to the M2 real-device gate in the development plan.
