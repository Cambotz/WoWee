// SMSG_GUILD_EVENT's bank half, and the balance it carries as text.
//
// The event list runs past 13: 14 to 19 are the guild bank's, broadcast to
// every member each time anyone touches it. Unhandled, they fell through to a
// branch that put the raw type and its first string into guild chat, so an
// active bank read as a wall of "Guild event 17: 000000000051451B".
//
// That string is the balance, written as sixteen zero-padded hex digits. The
// figure is only right if it is read as hex, and a server that sends the plain
// number must not be read as hex - which is what is pinned here.
#include <catch_amalgamated.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "core/application.hpp"
#include "game/world_packets.hpp"

namespace wowee {
namespace core {
Application* Application::instance = nullptr;
}
}

using namespace wowee::game;

namespace {

/// type, string count, then the strings.
GuildEventData parse(uint8_t type, const std::vector<std::string>& strings) {
    std::vector<uint8_t> bytes;
    bytes.push_back(type);
    bytes.push_back(static_cast<uint8_t>(strings.size()));
    for (const std::string& s : strings) {
        bytes.insert(bytes.end(), s.begin(), s.end());
        bytes.push_back(0);
    }
    wowee::network::Packet packet(0, std::move(bytes));
    GuildEventData data;
    REQUIRE(GuildEventParser::parse(packet, data));
    return data;
}

}  // namespace

TEST_CASE("the bank money event is a hex balance, not a chat line") {
    const GuildEventData data = parse(GuildEvent::BANK_MONEY_SET, {"000000000051451B"});
    CHECK(data.eventType == GuildEvent::BANK_MONEY_SET);
    CHECK(data.numStrings == 1);
    // 532g 61s 7c.
    CHECK(guildEventMoney(data.strings[0]) == 5326107u);
}

TEST_CASE("a balance written without padding is still hex when it says so") {
    CHECK(guildEventMoney("51451B") == 5326107u);
    CHECK(guildEventMoney("0x51451B") == 5326107u);
}

TEST_CASE("a plain decimal balance is not multiplied by sixteen") {
    CHECK(guildEventMoney("5326107") == 5326107u);
    CHECK(guildEventMoney("0") == 0u);
}

TEST_CASE("a balance that is not a number reads as nothing") {
    CHECK(guildEventMoney("") == 0u);
    CHECK(guildEventMoney("none") == 0u);
}

TEST_CASE("the event carries the trailing guid when one is there") {
    std::vector<uint8_t> bytes{GuildEvent::SIGNED_ON, 1};
    const std::string name = "Whitecheddar";
    bytes.insert(bytes.end(), name.begin(), name.end());
    bytes.push_back(0);
    for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<uint8_t>(0x51451Bull >> (i * 8)));

    wowee::network::Packet packet(0, std::move(bytes));
    GuildEventData data;
    REQUIRE(GuildEventParser::parse(packet, data));
    CHECK(data.strings[0] == name);
    CHECK(data.guid == 0x51451Bull);
}
