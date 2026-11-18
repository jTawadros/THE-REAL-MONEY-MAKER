# Heuristica

An artificial-life sandbox in C++20 where every character is **fully autonomous**. Nobody is
scripted and there is no player avatar. You watch a society run itself.

Characters get hungry, tired, cold, lonely and bored. Their emotions swing, and their **emotional
stability** can crack into mental breaks. They gather, fish, farm, craft tools and **build** huts,
storehouses, campfires, farms, walls, workshops and monuments. They **share** and trade, but they
also **steal**, brawl, take revenge, punish thieves, apologise and forgive. They gossip, court,
pair up, **mate**, raise children, teach them, grieve them, and pass on their genes and their
morals.

Right and wrong is never hard-coded. Every morally loaded choice goes through a **heuristic
conscience**: a weighted sum of harm, empathy, kinship, integrity, hunger, grudges, envy of
hoarders, learned social norms, fear of witnesses and current mood. A starving, low-empathy
character among strangers may decide that stealing is right. A well-fed, principled one never
would. The inspector shows the reasoning ("I'm starving and she has plenty", "she's family",
"too many eyes around").

No dependencies: a C++20 compiler and a terminal are all it needs.

## Build & run

```bash
make
```

```bash
./build/heuristica
```

```bash
./build/heuristica --headless --days 200 --seed 42
```

```bash
make test
```

CMake also works (`cmake -B build-cmake && cmake --build build-cmake`). `make debug` builds with
AddressSanitizer and UndefinedBehaviorSanitizer into `build-debug/`.

Use a terminal of at least 100×30; truecolor is detected automatically, and `--ascii` covers
fonts without Unicode.

## Watching

Press **Tab** to pick someone, then read the inspector: their needs, feelings, stress and
stability, the options they weighed with scores, and their *thought*, the reason for what
they're doing right now. Press **v** for the society view: population over time, how genes and
cultural norms have drifted since day 0, causes of death, top thieves and the biggest clans.
Press **e** to filter the event log (crime and justice, romance and births, deaths, or the
selected character).

| Key | Action |
|---|---|
| `space` | pause / resume |
| `+` `-` or `1`–`9` | speed ×1 … ×256 (`0` = ×¼) |
| `Tab` / `]`, `[` | next / previous character |
| `f` / `c` | follow selected / centre on them |
| arrows / `wasd` (`WASD` = fast) | pan |
| `v` | panel: inspector → society → agent list |
| `e` | event-log filter |
| `n` | single tick while paused |
| `?` / `q` | help / quit |

Map legend: `@` adults (coloured by clan), `o` children, `♣` trees, `*` berries, `^` rocks,
`~` fish, `"` herbs, `,` grain, `H` hut, `S` storage, `%` farm, `&` campfire, `#` wall,
`W` workshop, `M` monument, `+` construction site.

## Headless mode (balancing & experiments)

`--headless` prints a status line every few days and a final report covering:

- population trajectory, births and deaths by cause
- crime and kindness totals
- **trait evolution**: founders' average genes vs. the living population's
- **culture drift**: theft tolerance, violence tolerance, sharing and punishment norms
- clans and dynasties, notable individuals, and the last events

Use `--csv run.csv` to export the daily history and `--verbose` to stream every event.

Every run is deterministic for its seed, and different seeds produce very different societies.
For example:
- Seed 3 bred aggression *down* by 0.11 and stayed peaceful.
- Seed 21 drifted toward violence: the aggression gene rose, violence tolerance doubled, and
  people came to expect wrongdoing to be punished far more.
- Populations boom, crash in hard winters and recover. In testing they ran 400+ days and 12+
  generations without dying out.

## How the AI works

Each character runs a **utility AI** (`src/ai/Brain.cpp`):

1. About 40 `Behavior`s each propose concrete, scored `Action`s ("eat the berries 3 tiles away",
   "steal 2 food from Mara", "comfort my grieving sister").
2. Mental breaks distort the scores. A raging character sees violence everywhere; a despairing
   one only wants to mope.
3. **Commitment hysteresis** stops characters dithering between near-equal options.
4. **Bounded rationality**: a softmax over the top options, noisier for curious,
   less intelligent or stressed characters.

Pathfinding is A* with an octile heuristic. Adding a new thing characters can do means writing one
Behavior and one Action and registering it; the brain needs no changes.

See [docs/DESIGN.md](docs/DESIGN.md) for the full architecture, the morality heuristics, the
emotional-stability model and the tuning notes.

## Layout

```
src/core     Types, RNG, global tunables (Config.h)
src/world    terrain generation, resources, seasons, structures, A*
src/agent    genome (14 heritable traits), needs/emotions/memory/relationships, body & mind updates
src/ai       morality, brain, action helpers, survival/ (economy & building), social/ (interaction)
src/social   witnessing, gossip & reputation, norm learning, attraction
src/sim      tick loop, births, deaths & inheritance, spatial index, stats
src/render   terminal I/O, renderer, headless report
tests        dependency-free test suite (genome, world, A*, determinism, morality monotonicity)
```
