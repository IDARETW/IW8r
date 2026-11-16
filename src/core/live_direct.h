// IW8 Radiant - direct live link (no proxy): reads and writes a running Replay 1.20 process from the editor.
//
// Addresses are native Replay 1.20 RVAs (docs/research/05). Recompiled exes (game_dx12_ship_replay_dw.exe and
// friends, identity "RPLYCMP2" at image offset 0x40) move code and data; their `.rplmap` section maps every native RVA
// range to its new place (format: mw120rproxy replay_compiler_target.h). The map is read from the running process, so
// any build that carries it works without hard-coded RVAs. A stock exe (no identity) uses the native RVAs as they are.
//
// What the channel does (same memory the proxy uses, developer_ui.cpp):
//   player camera   [base+G_ENTITIES] -> entities; [entities+0x150] -> client; origin client+0x30, angles client+0x1CC
//   console command append to the native command buffer s_cbufMainBuffer {char* data; int capacity=0x10000; int size}
//                   only while it is empty (size 0). The game's CBUF critical section cannot be taken from outside,
//                   so this is a best-effort append; an empty buffer makes a collision with the game's own append
//                   unlikely, and the editor retries on the next click.
//   move player     writes the player-state origin (client+0x30). View angles are not written (delta angles UNKNOWN).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "geometry.h"

namespace iw8r::live {

// Native Replay 1.20 RVAs (docs/research/05; mw120rproxy replay_bindings.h / developer_ui.cpp).
inline constexpr uint32_t kRvaGEntities = 0xBC20F00;
inline constexpr uint32_t kRvaCbufMain = 0xD216D70;
inline constexpr uint32_t kNativeImageSize = 0x1324B000;

struct RvaRange {
    uint32_t begin = 0, end = 0, mappedBegin = 0;
};

// Parses an `.rplmap` section ("RPLYMAP2", version 2): contiguous ranges covering 0..kNativeImageSize.
// Returns false (and leaves `out` empty) when the bytes are not a valid map.
bool parseRplMap(const uint8_t* data, size_t size, std::vector<RvaRange>& out, std::string* err = nullptr);
// Native RVA -> RVA in the mapped image (identity when `ranges` is empty or the RVA is outside them).
uint64_t resolveRva(const std::vector<RvaRange>& ranges, uint64_t rva);

struct PlayerView {
    Vec3 origin;  // feet (player state origin)
    Vec3 angles;  // pitch, yaw, roll in degrees (game convention: positive pitch looks down)
};

// One attached game process. Windows only; elsewhere attach() always fails.
class DirectLink {
public:
    DirectLink() = default;
    ~DirectLink();
    DirectLink(const DirectLink&) = delete;
    DirectLink& operator=(const DirectLink&) = delete;

    bool attach(uint32_t pid, std::string* err = nullptr);
    void detach();
    bool attached() const { return process_ != nullptr; }
    uint32_t pid() const { return pid_; }
    bool recompiled() const { return !ranges_.empty(); }  // the exe carries an .rplmap
    uint64_t base() const { return base_; }

    // Null pointers (front end, no match) give nullopt with err "not in a match".
    std::optional<PlayerView> readPlayer(std::string* err = nullptr) const;
    bool sendCommand(const std::string& text, std::string* err = nullptr);
    bool setPlayerOrigin(const Vec3& origin, std::string* err = nullptr);
    // True when the native command buffer is empty (sendCommand would accept text now).
    bool commandBufferIdle() const;

    // Native-RVA reads (devmap state, docs/research/65). `nativeRva` is a stock Replay 1.20 RVA; recompiled exes are
    // resolved through their .rplmap. Dvar reads take the RVA of the dvar_t* global; the value is at +valueOffset.
    std::optional<int32_t> readI32(uint32_t nativeRva) const;
    std::optional<uint8_t> readU8(uint32_t nativeRva) const;
    std::optional<bool> readDvarBool(uint32_t nativeRvaOfDvarPtr, uint32_t valueOffset = 0x28) const;
    std::optional<std::string> readDvarString(uint32_t nativeRvaOfDvarPtr, uint32_t valueOffset = 0x28, size_t maxLen = 64) const;
    bool inMatch() const;  // the local player entity exists (g_entities -> client)

private:
    bool read(uint64_t addr, void* out, size_t n) const;
    bool write(uint64_t addr, const void* in, size_t n);
    uint64_t addr(uint32_t nativeRva) const { return base_ + resolveRva(ranges_, nativeRva); }
    bool client(uint64_t& clientOut, std::string* err) const;

    void* process_ = nullptr;
    uint32_t pid_ = 0;
    uint64_t base_ = 0;
    std::vector<RvaRange> ranges_;
};

}  // namespace iw8r::live
