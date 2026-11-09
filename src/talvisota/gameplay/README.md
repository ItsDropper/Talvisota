# Gameplay

Owns simulation orchestration and game rules shared across missions. Keep it deterministic and independent of graphics APIs and OS handles.
Gameplay orchestration composes `Talvisota::Entities`, `Talvisota::Combat`, and
`Talvisota::Missions`; it does not own their domain implementations.
