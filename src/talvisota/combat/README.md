# Combat

Owns game-specific combat rules, damage resolution, hit evaluation, and status effects. Weapon presentation and animation are separate concerns.
The implementation lives in `CombatSimulation.hpp/.cpp` and is built as
`Talvisota::Combat`. It depends on the entity registry, not on gameplay orchestration.
