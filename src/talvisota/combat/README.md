# Combat

Owns game-specific combat rules, damage resolution, hit evaluation, and status effects. Weapon presentation and animation are separate concerns.
The implementation lives in `CombatSimulation.hpp/.cpp` and is built as
`Talvisota::Combat`. It depends on the entity registry, not on gameplay orchestration.


## Runtime use

The game runtime advances this subsystem on the fixed gameplay step. Player fire and reload input, weapon inventory switching, faction-aware NPC fire commands, health/ammunition HUD state, and combat-to-objective events all use this subsystem rather than a parallel combat implementation. Weapon statistics are game-balance parameters, not real-world performance claims.