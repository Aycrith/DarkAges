// [NETWORK_AGENT] Placeholder protocol header
#ifndef DARKAGES_PROTOCOL_HPP
#define DARKAGES_PROTOCOL_HPP

namespace DarkAges {
namespace Protocol {
enum class MessageType : uint8_t {
    Unknown = 0,
    PlayerMove = 1,
    PlayerUpdate = 2,
    CombatHit = 3,
    ZoneTransfer = 4
};
} // namespace Protocol
} // namespace DarkAges

#endif // DARKAGES_PROTOCOL_HPP