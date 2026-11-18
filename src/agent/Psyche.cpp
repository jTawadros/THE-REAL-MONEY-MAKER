// Heuristica — display names for psyche enums (needs, emotions, breaks, memories, skills, life stages).
#include "agent/Psyche.h"

#include "agent/Agent.h"

namespace heur {

const char* needName(Need n) {
    switch (n) {
        case Need::Food: return "Food";
        case Need::Rest: return "Rest";
        case Need::Social: return "Social";
        case Need::Warmth: return "Warmth";
        case Need::Fun: return "Fun";
        case Need::Intimacy: return "Intimacy";
        default: return "?";
    }
}

const char* emotionName(Emotion e) {
    switch (e) {
        case Emotion::Joy: return "Joy";
        case Emotion::Sadness: return "Sadness";
        case Emotion::Anger: return "Anger";
        case Emotion::Fear: return "Fear";
        case Emotion::Guilt: return "Guilt";
        case Emotion::Pride: return "Pride";
        case Emotion::Envy: return "Envy";
        default: return "?";
    }
}

const char* breakTypeName(BreakType b) {
    switch (b) {
        case BreakType::None: return "none";
        case BreakType::Rampage: return "rampage";
        case BreakType::Despair: return "despair";
        case BreakType::Panic: return "panic";
        case BreakType::Kleptomania: return "kleptomania";
        default: return "?";
    }
}

const char* memoryTypeName(MemoryType m) {
    switch (m) {
        case MemoryType::ReceivedGift: return "received a gift";
        case MemoryType::GaveGift: return "gave a gift";
        case MemoryType::StolenFrom: return "was robbed";
        case MemoryType::Stole: return "stole";
        case MemoryType::WitnessedTheft: return "saw a theft";
        case MemoryType::Attacked: return "was attacked";
        case MemoryType::AttackedSomeone: return "attacked someone";
        case MemoryType::WitnessedAttack: return "saw an attack";
        case MemoryType::Chatted: return "chatted";
        case MemoryType::Comforted: return "was comforted";
        case MemoryType::Mated: return "mated";
        case MemoryType::ChildBorn: return "had a child";
        case MemoryType::LovedOneDied: return "lost a loved one";
        case MemoryType::Punished: return "was punished";
        case MemoryType::PunishedSomeone: return "punished someone";
        case MemoryType::Forgave: return "forgave";
        case MemoryType::BuiltTogether: return "built together";
        case MemoryType::Traded: return "traded";
        case MemoryType::Rejected: return "was rejected";
        case MemoryType::Taught: return "was taught";
        default: return "?";
    }
}

const char* skillName(Skill s) {
    switch (s) {
        case Skill::Foraging: return "Foraging";
        case Skill::Woodcutting: return "Woodcutting";
        case Skill::Mining: return "Mining";
        case Skill::Building: return "Building";
        case Skill::Farming: return "Farming";
        case Skill::Crafting: return "Crafting";
        case Skill::Fighting: return "Fighting";
        case Skill::Socializing: return "Socializing";
        default: return "?";
    }
}

const char* lifeStageName(LifeStage s) {
    switch (s) {
        case LifeStage::Child: return "child";
        case LifeStage::Adult: return "adult";
        case LifeStage::Elder: return "elder";
        default: return "?";
    }
}

}  // namespace heur
