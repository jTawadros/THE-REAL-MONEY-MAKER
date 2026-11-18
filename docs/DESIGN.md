# Heuristica — Design

Heuristica is a terminal-rendered artificial-life sandbox written in C++20 (no dependencies).
Every character is fully autonomous: no scripts, no player control. Each tick they perceive the
world, score every option they could take with **utility heuristics**, and act. They get hungry,
tired, cold, lonely and bored; their emotions swing and their **emotional stability** can crack into
mental breaks. They gather, farm, craft, build huts/storage/campfires/walls/monuments, share, trade,
steal, fight, punish, forgive, gossip, court, mate, raise children and teach them. Right and wrong is
never hard-coded — it is the output of a heuristic conscience (`ai/Morality`) whose weights come from
genes, emotions, relationships and culturally learned norms that drift as agents watch each other.

## Module map & ownership

| Path | Contents |
|---|---|
| `src/core/` | `Types.h` (Vec2i, Inventory, items), `Random.h`, `Config.h` (global tunables) |
| `src/world/` | `World` (terrain gen, resources, seasons, structures), `Pathfinding` (A*) |
| `src/agent/` | `Genome` (14 heritable traits), `Psyche.h` (needs, emotions, memory, relations, norms, skills), `Agent` (body & mind updates) |
| `src/ai/` | `Morality` (judge/appraise), `Action.h` (Behavior/Action/Proposal), `ActionHelpers`, `Brain` (utility selection), `SurvivalBehaviors.cpp`, `SocialBehaviors.cpp` |
| `src/social/` | `Social` (witnessing, gossip/reputation, norm learning, attraction, mate acceptance) |
| `src/sim/` | `Simulation` (tick loop, births, deaths, spatial index, log, stats/history) |
| `src/render/`, `src/main.cpp` | ANSI terminal renderer, keyboard input, CLI, headless reports |

Headers listed above are the **frozen contract**. Module-specific tunables live in an anonymous
namespace inside that module's `.cpp`. Private helper headers are fine inside your own module dir.

## Tick order (`Simulation::step`)

1. `world.update(now)` — regrowth, farm growth, campfire fuel, decay.
2. Rebuild spatial index (agent positions as of tick start).
3. For each living agent in shuffled order: `updateBody` → `updateMind` → `brain.think` → `brain.act`.
4. Births (pregnancies past `dueTick`).
5. Deaths: log cause, grief for family/friends, inheritance (partner → oldest living child), home &
   owned structures pass to heirs or become communal, partner link cleared, agent removed.
6. Once per day: push a `Sample` into history.

## Time scale

`cfg::kTicksPerDay = 240`. Seasons last 8 days; a year is 32 days. Adulthood at 12 days, elder at 55,
old-age hazard after 65. Pregnancy 3 days. An adult needs ≈ 2.4 food units/day
(`kFoodDecay / kFoodPerUnit * kTicksPerDay`). Winter has almost no wild regrowth except fish, so
survival needs storage, farms and/or sharing — or stealing.

## The decision loop (utility AI)

```
for each Behavior b:  b.propose(self, sim, out)      // push scored Proposals {score, Action, reason}
brain: discard score <= 0
       breakdown modifiers (Rampage ⇒ attack/vandalize ×3, Despair ⇒ mope, Panic ⇒ flee, Kleptomania ⇒ steal)
       commitment hysteresis: same kind as current action gets +cfg::kCommitmentBonus; if it wins, keep
         the current action object (don't lose progress)
       bounded rationality: softmax-ish pick among top 3; noise ↑ Curiosity, ↓ Intelligence & Stability
       record top options + reason in agent.considered / agent.thought (shown in inspector)
```

### Scoring conventions (keep behaviours comparable!)

* Score ≈ `need urgency × weight` (0..1) plus trait modifiers, times `distanceFactor` for far targets.
  `Needs::urgency()` is convex, so a nearly-empty need dominates.
* Idle/wander baseline ≈ 0.05–0.10. Leisure (play, chat, art) ≈ 0.1–0.5. Productive work 0.2–0.6.
  Emergencies (starving with food reachable, being attacked) ≈ 0.9–1.5.
* Morally loaded proposals call `judge()` and add `verdict.utility` (typically −1..+0.5). Put
  `verdict.rationale` in the proposal reason so the UI shows *why* someone stole or shared.
* Children (`LifeStage::Child`) only: eat, sleep, forage, play, wander, follow parent, beg, chat,
  warm up, mope, flee. Never build, mate, court, steal from storage, attack (they may still pickpocket
  if desperate — the conscience decides).

### Action conventions

* Store target **ids** (`AgentId`, `StructureId`, `Vec2i`), never pointers, across ticks. Re-validate
  every tick: `sim.agent(id)` returns nullptr for dead agents → `Failed`.
* Move with `stepToward(self, sim, goal, range)`; `Blocked` ⇒ return `Failed`. Interact at range 1.
* Duration-based work: count ticks inside the action; skill/trait/tools scale speed. Call
  `self.practice(skill, x)`.
* When a morally loaded deed actually happens: rebuild a `DeedContext` (pointers valid this tick
  only), `self.feel(Guilt, verdict.guilt)` / `self.feel(Pride, verdict.pride)`, update
  `self.stats` and `sim.stats()` counters, `sim.log(...)` one line, then `broadcastDeed(...)`.
  **The action logs the deed itself; `broadcastDeed` never re-logs it** (it may log rare reactions).
* `actionKind == kinds::Sleep` means asleep: sleepers don't witness and can be robbed more easily.
* Counters: actions own thefts/gifts/fights/punishments/forgivenesses/trades/matings/structuresCompleted;
  `Agent::updateMind` owns breakdowns; `Simulation` owns births/deaths/causes/maxGeneration.

## Morality heuristics (`judge`)

Weighted sum, each term in roughly −1..1:

* **Harm** — `−Empathy × harm × (1 + target's need)`, doubled for family, ×1.3 for same clan.
* **Rule** — `−Integrity × taboo(deed)` (steal 0.6, attack 0.8, kill 1.0, adultery 0.5, vandalize 0.5).
* **Necessity** — desperation (`urgency(Food)`) shrinks the rule and harm terms: "I have to eat".
* **Reciprocity / revenge** — grievance toward target × Vengefulness offsets cost ("he stole from me").
  Gratitude (positive `balance`) adds cost to wronging a benefactor.
* **Envy / fairness** — target holds far more food than they need ⇒ Greed/Envy justify taking.
* **Norms** — `theftTolerance` / `violenceTolerance` lower costs; `punishmentExpectation` raises risk.
* **Witness risk** — `witnesses × (1 − Bravery) × punishmentExpectation`.
* **Emotion** — Anger lowers violence cost; Guilt raises transgression cost; Joy raises generosity.
* **Prosocial reward** — Share/Gift/Comfort/Help: `Empathy × benefit × target need` + reputation
  gain; `sharingExpectation` adds duty; Greed and own scarcity subtract.

Observers run `appraise()`; consequences flow into relationships, grievances (revenge), gossip
(reputation) and norms (culture). Children inherit the average of their parents' norms + noise, so
societies develop distinct moral cultures over generations while genes evolve under selection.

## Emotional stability

Negative emotions and unmet needs build `stress`; positive mood, sleep, company, art and comfort
relieve it. The `Stability` gene scales both emotion intensity (`feel`) and stress gain. Past
`cfg::kBreakdownThreshold` an agent may break: the dominant emotion picks the break type
(Anger → Rampage, Sadness → Despair, Fear → Panic, Envy → Kleptomania). Breaks last a quarter to
half a day and end in catharsis (stress drops).

## Balancing notes (from multi-seed headless sweeps)

* **Population control is heuristic, not a cap.** Conception odds per mating scale with libido, the
  couple's food security, young children already at foot, season (autumn ×0.8, winter ×0.6) and
  local crowding (`÷ 1 + (neighbours within 12 − 18) / 8`). With a 5-day minimum gap between a
  mother's conceptions, populations oscillate between ~30 and ~110 and survive 400+ days.
* **Feuds burn out instead of running away.** Each landed revenge hit cuts the attacker's grievance
  (×0.6 − 0.05). Victims who know they wronged the attacker first (tit-for-tat: 0.75–1.0 weight;
  prior theft: 0.4–1.0 weight scaled by Integrity) take a much smaller new grudge, and kin take
  smaller grudges when they can see the attacker had a reason. Without this, a single punishment
  escalated into 20-day blood feuds involving whole clans.
* **Hunger only excuses deeds that feed you.** The necessity term that shrinks harm/rule costs is
  multiplied by the deed's `selfGain` (and ×0.4 for killing), so starving ragers no longer murder
  for nothing.
* **Mood is centred on "okay".** Needs at 0.65 satisfaction are mood-neutral, and only mood above
  +0.15 heals stress, so stress tracks hardship: winter peaks around 0.1–0.2 for the population,
  with individual breakdowns during famines, feuds and grief.
* **Hot paths.** `World::nearWarmth/nearMonument` scan tile ids directly, and `foodOwned` uses a
  per-tick owner→food index. Throughput is ~3,300 ticks/s at ~75 agents (≈ 14 game-days/second).
