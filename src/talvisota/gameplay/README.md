# Gameplay

Owns simulation orchestration and game rules shared across missions. Keep it deterministic and independent of graphics APIs and OS handles.
Gameplay orchestration composes `Talvisota::Entities`, `Talvisota::Combat`, and
`Talvisota::Missions`; it does not own their domain implementations.


## Battlefield integration

Authored mission actors are spawned as gameplay infantry entities and registered with the combat simulation. The player receives a Mosin M/27, Suomi KP/-31, and Lahti-Saloranta M/26 loadout; weapon switching preserves each weapon's magazine and reserve ammunition. First-person aim selection uses the camera ray, target bounds, weapon range, and the active Sakkola collision geometry. The deterministic battlefield AI selects living opponents by faction and range, respects authored objective order, and lets multiple friendly support actors engage only after the player reaches the active engagement. The engine-enabled game runtime renders authored infantry as faction-colored, low-poly winter soldiers in the scene; defeated actors are removed from the active presentation. Combat defeats are translated into ordered mission events.