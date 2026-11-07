# Sakkola environment reference

## Fixed first-area choice

**Sakkola Kirkonkylä and the north shore of Suvanto, Karelian Isthmus, Finnish territory, winter 1939.**

This is the fixed first environment. Do not move the initial area to a different parish or replace it with a generic Central Finnish forest.

## Primary cartographic source

The National Land Survey of Finland's *Karjalan kartat* collection contains pre-Winter-War topographic and parish maps of the Karelian Isthmus and Ladoga Karelia. Its 1:20,000 topographic sheets show contour lines, place names, settlement, roads, and terrain conditions; each sheet covers approximately 10 km by 10 km.

The Sakkola sheet is listed in the official Topografinen_20000 archive as:

- Sheet: `404206_SAKKOLA.tif`
- Official directory: https://vanhatpainetutkartat.maanmittauslaitos.fi/karjalankartat/Topografinen_20000/
- National Land Survey information: https://www.maanmittauslaitos.fi/en/e-services/karelian-maps
- Map download instructions: https://www.maanmittauslaitos.fi/en/maps-and-spatial-data/maps/view-maps/how-start-using-old-maps-karelia

The National Land Survey's archive instructions request attribution to the National Land Survey / Finnish Defence Forces when map material is reproduced. This repository does not redistribute the historical raster; it is a research reference only.

## Geographic and settlement context

Sakkola lay on the eastern Karelian Isthmus around the Suvanto water system. Suvanto divided the parish into northern and southern areas; the church village was north of the lake. The parish included agricultural villages, cultivated fields, farmsteads, roads, forest, and shore settlements. The water system and its shore are defining geographic features, not interchangeable generic lake scenery.

Useful historical context:

- Sakkola parish overview and Suvanto geography: https://www.karjalainennuorisoliitto.fi/minunjuureni/sakkola/
- Sakkola historical overview: https://www.luovutettukarjala.fi/pitajat/sakkola/sakkolahist.htm
- The National Land Survey's pre-war map collection remains the primary source for exact contours, roads, and settlement placement.

## Current authored blockout

The current region is a **512 x 512 metre playable slice**, not a full 10 km map sheet.

- 129 x 129 terrain heightfield, 4 m spacing.
- Terrain OBJ vertex grid matches the collision/query heightfield.
- Frozen Suvanto ice surface at the southern edge.
- A packed rural track and frozen agricultural clearings.
- 170 vegetation placements using five distinct models: Norway spruce, Scots pine, silver birch, aspen, and alder.
- Eight granite boulder placements.
- Five period-inspired structures: a church, two farmhouses, a barn, and a sauna.
- 50 forest-floor and farmstead props: fallen logs, stumps, woodpiles, a well, fence sections, and shoreline reeds.
- Independent collision volumes and a directed village footpath graph.
- Tree, boulder, fallen-log, stump, and shoreline-reed bases aligned to the sampled terrain; tree, log, and stump collision bottoms are regression-tested against the same surface.

The landscape composition reflects the documented relationship between the church village, cultivated ground, mixed forest, and Suvanto. The current elevations, exact shoreline curve, track alignment, and building coordinates are **authored approximations**, not values digitized from the official map. They must not be described as georeferenced or historically exact.

## Visual quality work still required

The current geometry is a substantive environment foundation, not a photorealism claim. Remaining work includes:

1. Digitize the Sakkola sheet's contours and shoreline into a georeferenced terrain source, with a documented coordinate transform and vertical datum.
2. Reconcile village roads, field boundaries, farmstead footprints, and shoreline placement against the historical sheet.
3. Calibrate the current 41 generated 1024x1024 seamless PBR maps (albedo, tangent-space normal, roughness, AO) for bark, pine needles, birch bark, aspen bark, alder bark, granite, snow, packed snow, ice, and reeds; replace or augment procedural first-pass detail with reference-based hand-authored or properly licensed scanned materials where they materially improve realism.
4. Add more local model variation (tree age, crown shape, branch damage, deadwood, stumps, saplings, and rock silhouettes) without turning the forest into a uniform scatter.
5. Validate material texture decoding, mip generation, tangent frames, shadow response, snow accumulation, and the final environment on real D3D11 and D3D12 hardware.
6. Capture a genuine renderer framebuffer only after the environment reaches the visual review threshold. Never substitute a generated or composited image for a renderer capture.

## Provenance

All newly authored OBJ and MTL content in this region is original project content. The historical map is used as a reference and is not bundled. Keep the map attribution and its terms attached to any future derived data; confirm reuse rights before distributing a georeferenced derivative.
