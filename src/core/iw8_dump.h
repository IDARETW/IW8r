// IW8 Radiant Suite - x64-zt IW8 asset dump container ("IW8D", version 1), Replay 1.20.
//
// x64-zt `-game iw8 dumpzone` writes one file per asset: u32 magic 'IW8D', u32 version 1, i32 asset type,
// u32 record count, then records { u8 kind; data|string|asset|script_string: u64 value, u64 length, bytes;
// runtime|temp: u64 value; offset: u64 value, u64 delta; null: nothing } (x64-zt src/games/iw8/zone/streams.hpp
// dump_stream::finish). For map_ents (type 29) record 0 is the 1032-byte MapEnts header (numEntityChars u32 at
// +16), record 1 the name and record 2 the NUL-terminated entity string (verified on stock mp_shipment).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

struct Iw8DumpRecord {
    enum Kind : uint8_t { Null, Data, Runtime, Offset, String, Asset, ScriptString, Temp };
    Kind kind = Null;
    uint64_t value = 0;
    uint64_t delta = 0;  // Offset only
    std::string bytes;   // Data, String, Asset, ScriptString
};

struct Iw8Dump {
    int32_t type = -1;
    std::vector<Iw8DumpRecord> records;
};

bool parseIw8Dump(const std::string& file, Iw8Dump& out, std::string* err = nullptr);
std::string serializeIw8Dump(const Iw8Dump& d);

constexpr int32_t kIw8MapEntsType = 29;

// MapEnts helpers: read / replace the entity string and keep numEntityChars in step.
bool mapEntsEntityString(const Iw8Dump& d, std::string& out, std::string* err = nullptr);
bool setMapEntsEntityString(Iw8Dump& d, const std::string& entityString, std::string* err = nullptr);

}  // namespace iw8r
