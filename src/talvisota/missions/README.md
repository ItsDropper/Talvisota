# Missions

Owns mission lifecycle, objective state, triggers, and authored event orchestration. Reusable mechanics remain outside individual mission implementations.
The mission state machine and authored-content adapter live here and are built as
`Talvisota::Missions`. This module depends on validated content contracts, not on the
gameplay simulation layer.
