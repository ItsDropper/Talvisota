# Content library

The backend-independent `Talvisota::Content` library owns stable asset IDs, the versioned content manifest, dependency resolution, provenance metadata, typed PBR/world/region definitions, and strict mission/faction/character/equipment parsers. Region composition is loaded by Talvisota::World and is validated by the shipped-content and package validators. It does not include Direct3D or VantaEngine types.

Generic OBJ/glTF/GLB model import, WIC image decoding, CPU asset stores, and deterministic texture processing belong to VantaEngine. Talvisota's optional engine-backed validation target connects the content manifest to those services without introducing renderer types into the core content library.
