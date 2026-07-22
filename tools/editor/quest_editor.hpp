#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace editor {

enum class QuestObjectiveType {
    KillCreature,
    CollectItem,
    TalkToNPC,
    ExploreArea,
    EscortNPC,
    UseObject
};

struct QuestObjective {
    QuestObjectiveType type = QuestObjectiveType::KillCreature;
    std::string description;
    std::string targetName;
    uint32_t targetCount = 1;
};

struct QuestReward {
    uint32_t xp = 100;
    uint32_t gold = 0;
    uint32_t silver = 0;
    uint32_t copper = 0;
    std::vector<std::string> itemRewards;
};

struct Quest {
    uint32_t id = 0;
    std::string title = "New Quest";
    std::string description;
    std::string completionText;
    uint32_t requiredLevel = 1;
    uint32_t questGiverNpcId = 0;
    uint32_t turnInNpcId = 0;
    std::vector<QuestObjective> objectives;
    QuestReward reward;
    uint32_t nextQuestId = 0; // chain link
};

class QuestEditor {
public:
    void addQuest(const Quest& q);
    void removeQuest(int index);
    Quest* getQuest(int index);
    const std::vector<Quest>& getQuests() const { return quests_; }
    size_t questCount() const { return quests_.size(); }

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
    void clear() { quests_.clear(); nextId_ = 1; }

    // Quest chain validation
    bool validateChains(std::vector<std::string>& errors) const;

    Quest& getTemplate() { return template_; }

private:
    std::vector<Quest> quests_;
    Quest template_;
    uint32_t nextId_ = 1;
};

} // namespace editor
} // namespace wowee
