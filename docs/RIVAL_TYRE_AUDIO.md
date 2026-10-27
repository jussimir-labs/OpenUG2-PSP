# Rival tyre audio

Racing opponents and free-roam random-encounter rivals now have their own spin, sliding and braking/locked-tyre emitters. Ordinary traffic is excluded. This change supplies sound only: it does not change AI driving, steering, tyre forces, braking, contact responses, career or savedata.

## Original source qualification

The local, symbol-bearing GameCube executable was inspected before implementation. Its pseudo-C is not part of this repository. The retained audit is `reports/closed-beta-20261025/rival-tyres-20261026/source-audit/`.

- `CSTATEMGR_AICar::EnterWorld` (`800aa0bc`) creates AI tuner-car states with mask `0x802d`. Skid effect type `0x00010050` selects bit 5, which is enabled.
- `RaceStarter::AddAIOpponentCars` (`8011a664`) and `AddRandomEncounterCars` (`8011a7d8`) both create driver type 2. The same audio manager path applies to both. Ordinary type-3 traffic has a separate mask without this skid effect.
- `CARSFX_Skids::SetupSFX` (`8009eb64`) selects one emitter for AI type 2. `SFXCTL_Skid::GetSkidMag` (`8008f444`) averages the two integer side magnitudes for each of its three components. The AI ground gate uses the left-side wheel-contact OR; terrain selection uses the original consensus/history rule.
- The AI skid and engine outputs attach to the same 3D controller record. Its distance input uses the horizontal plane (GU x/z), ignoring height. Its chase-view distance envelope reaches full gain at 3 m, with directional endpoints 140 m ahead, 50 m at either side and 100 m behind. Shape 6 corresponds to the squared inverse cosine-table curve. The port uses the continuous sine equivalent; the source audit records the remaining lookup/interpolation difference. Stereo pan and bounded Doppler retain the existing port convention and are not claimed to be bit-identical to the original spatial backend.
- Skid channel levels are separate from the engine and player channels: normal/URL -14.75 dB, Drift -9.75 dB, Drag -17.75 dB, before options/master gain and individual component trims.

## Implementation and ownership

`tyres.c` shares the existing magnitude calculation while storing independent smoothing and terrain histories for three rival slots. Changing a slot's owner or bank retires its previous history. Pause or an inactive source mutes immediately; a paused source retains its filters. One integer-averaged mono source is spatialized per car.

`audio.c` preloads the four existing skid banks into immutable descriptors and payloads. They do not alias the player's mutable skid bank. The normal allocator retains its original 24 voices; nine additional voices are reserved for the three rival emitters. Rebinding a reserved voice requires the mixer's own acknowledged hold. A failed hold does not authorize descriptor replacement. Payloads are released after mixer shutdown. Compact mode stores the additional banks as IMA; this is not a PSP-1000 memory or runtime approval.

`rival_tyres.c` adapts the existing car state without modifying a `Car`. Physical race opponents supply their real contacts, braking input and body acceleration; wheel-motion and grip-usage estimates use the existing port helpers. The latter remain sound estimates rather than an original per-wheel force solver. Free-roam rivals follow the port's road-graph driver and have no suspension/wheel-force simulation: their audio view derives motion from consecutive velocities/headings, samples terrain beneath each wheel, treats their road-following wheels as grounded and infers a brake fraction from the rail driver's existing 8 m/s² deceleration bound. These are explicit port input adaptations, not newly simulated physics. A placement discontinuity clears audio history rather than emitting a false acceleration transient.

The race and free-roam callers select the corresponding rival fleet model's regular/truck bank. Sample decoding and the speed-dependent sample pitch retain the existing port tyre renderer; the original AEMS skid patch is not reconstructed in full by this change. No per-frame asset load or conversion is introduced. No new asset format or full conversion is required.

## Verification and limits

`tools/test_rival_tyres_adapter.py` compiles the actual adapter, acceleration and wheel-motion helpers with address/undefined-behavior sanitizers. It checks directional cutoffs, per-mode channel gain, pan convention, the original planar distance, pause, finite inputs, rail telemetry and byte preservation of the source car. `tools/test_rival_tyres_audio.py` covers the mixer/controller side, including private banks and reserved voices.

Directed PPSSPPHeadless results and exact build identities belong in the retained per-build report. A completed emulator run, nonzero magnitude, or gain above a mixer threshold is not independent proof of subjective audibility, absence of clicks, performance at 30 fps, PSP-1000 compatibility or physical Memory Stick behavior. RC13 remains preserved as the previous reference. No automatic card transfer, installation or physical test is authorized by this implementation.
