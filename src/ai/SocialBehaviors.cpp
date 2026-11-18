// Heuristica — registration of every social behaviour (implementations live in ai/social/*.cpp).
//
// Behaviour        kind            typical score  notes
// ---------------- --------------- -------------- ---------------------------------------------------
// chat             chat            0.08 – 0.6     loneliness × Sociability × liking; gossip both ways
// comfort          comfort         0.15 – 0.8     sad/stressed friend or kin; + conscience
// teach            teach           0.10 – 0.45    skilled adult → child/friend with a skill gap
// mourn            mourn           0.10 – 0.6     recent LovedOneDied; ×3 under Despair (Brain)
// follow_parent    follow_parent   0.20 – 0.8     children far (>6) from a parent, more when afraid / at night
// share            share           0.10 – 0.9     hungry neighbour, my spare food; + conscience
// gift             gift            0.05 – 0.6     repay debts, friends, partner, courtship; + conscience
// trade            trade           0.12 – 0.6     barter surplus for a want with a trusting partner
// feed_child       feed_child      0.25 – 1.45    own hungry child; very high for empathetic parents
// beg              beg             0.10 – 0.8     hungry child / desperate humble adult; giver judges Share
// steal            steal           <0 – 1.2       hunger/greed/kleptomania + conscience − fear of being caught
// steal_storage    steal_storage   <0 – 1.1       private store not mine/family's; + conscience
// vandalize        vandalize       <0 – 0.7       deep grudge or Rampage (×3 in Brain)
// attack           attack          <0 – 1.3       revenge, rampage, robbery, defending kin, self-defence
// flee             flee            0.4 – 1.6      being attacked / hostile nearby / Panic
// confront         confront        <0 – 0.9       punish a known wrongdoer; restitution or a short fight
// apologize        apologize       0.1 – 0.7      guilt over a recent theft/attack; restitution
// forgive          forgive         <0 – 0.5       let go of a grudge (kin, amends, low Vengefulness)
// court            court           <0 – 0.7       attraction × Libido × intimacy need; adultery judged
// mate             mate            0.1 – 0.8      with a mutual partner; may conceive
#include "ai/Action.h"
#include "ai/social/SocialCommon.h"

namespace heur {

void registerSocialBehaviors(BehaviorList& out) {
    social::addConversationBehaviors(out);
    social::addExchangeBehaviors(out);
    social::addCrimeBehaviors(out);
    social::addConflictBehaviors(out);
    social::addRomanceBehaviors(out);
}

}  // namespace heur
