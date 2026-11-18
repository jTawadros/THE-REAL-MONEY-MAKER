// Heuristica — survival & economy behaviours: everything agents do to stay alive and to build.
//
// The implementation is split by theme under src/ai/survival/ (see SurvivalCommon.h for the file map).
// Behaviours only read the agent & world and push scored Proposals; Actions do all mutation when they run.
// Score bands (see docs/DESIGN.md): idle/wander 0.05-0.1, leisure 0.1-0.5, productive work 0.2-0.6,
// emergencies 0.9-1.5. Every weight lives in the anonymous-namespace tunables block of its file.
#include "ai/Action.h"
#include "ai/survival/SurvivalCommon.h"

namespace heur {

void registerSurvivalBehaviors(BehaviorList& out) {
    survival::registerNeedBehaviors(out);     // Eat, EatFromStorage, Sleep, WarmUp, Heal
    survival::registerGatherBehaviors(out);   // Forage, Fish, Chop, Mine, GatherHerbs
    survival::registerBuildBehaviors(out);    // Build (start / continue / repair), HelpBuild
    survival::registerEconomyBehaviors(out);  // Farm, Craft, Deposit, Withdraw
    survival::registerLeisureBehaviors(out);  // Idle, Wander, Explore, GoHome, Mope, Play, MakeArt
}

}  // namespace heur
