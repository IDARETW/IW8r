"""Client side of roadmap F.11 (game modes of a custom map): DW_MapAvailability.cpp reads "gameTypes" from map.json.
Run once from E:\\IW8-Radiant. Writes the before/after diff to docs/research/79_client/DW_MapAvailability.cpp.diff."""
import difflib, io, os

p = 'E:/DW/IW8/integration/replay/DW_MapAvailability.cpp'
raw = io.open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in raw
s = raw.replace('\r\n', '\n')
before = s
pairs = [
(r'''        char id[16], title[64], compass[32], loading[32];
        wchar_t directory[260];
        float ladderRungDistance; // map.json "ladderRungDistance"; 12 by default
    };''',
 r'''        char id[16], title[64], compass[32], loading[32];
        wchar_t directory[260];
        float ladderRungDistance; // map.json "ladderRungDistance"; 12 by default
        char gameTypes[96];       // map.json "gameTypes"; empty: Team Deathmatch only
    };'''),
(r'''    // Reads "ladderRungDistance" from map.json: the rung spacing of a converted''',
 r'''    // Reads "gameTypes" from map.json: the game types the package carries spawns
    // and objects for, written the way the native map rows list them
    // ("war dm dom"). Lower-case names of at most 11 characters separated by
    // single spaces; anything else leaves the list empty, and the row then
    // offers Team Deathmatch only, as before.
    void ReadGameTypes(const wchar_t *path, char (&types)[96])
    {
        types[0] = 0;
        auto *file = OpenRead(path);
        if (file == reinterpret_cast<void *>(-1))
            return;
        static char text[4096 + 16]; // key compares may read past the terminator
        unsigned long read = 0;
        ReadFile(file, text, 4095, &read, nullptr);
        CloseHandle(file);
        text[read] = 0;
        const char key[] = "\"gameTypes\"";
        const char *at = text;
        while (*at && memcmp(at, key, sizeof(key) - 1))
            ++at;
        if (!*at)
            return;
        at += sizeof(key) - 1;
        while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n' || *at == ':')
            ++at;
        if (*at++ != '"')
            return;
        unsigned used = 0, word = 0;
        for (; *at && *at != '"'; ++at)
        {
            const bool letter = (*at >= 'a' && *at <= 'z') || (*at >= '0' && *at <= '9') || *at == '_';
            const bool space = *at == ' ' && word; // one space, and only after a name
            if ((!letter && !space) || used + 2 >= sizeof(types) || (letter && ++word > 11))
            {
                types[0] = 0;
                return;
            }
            if (space)
                word = 0;
            types[used++] = *at;
        }
        if (*at != '"' || !word) // unterminated, empty, or a trailing space
            used = 0;
        types[used] = 0;
    }

    // Reads "ladderRungDistance" from map.json: the rung spacing of a converted'''),
(r'''                ReadTitle(path, map.title);
                map.ladderRungDistance = ReadLadderRungDistance(path);
            }
            else
                map.ladderRungDistance = 12.0f;''',
 r'''                ReadTitle(path, map.title);
                map.ladderRungDistance = ReadLadderRungDistance(path);
                ReadGameTypes(path, map.gameTypes);
            }
            else
            {
                map.ladderRungDistance = 12.0f;
                map.gameTypes[0] = 0;
            }'''),
(r'''            Copy(reinterpret_cast<char *>(row.native + 0x40), 32, "war");
            Copy(row.gameTypes, sizeof(row.gameTypes), "war");''',
 r'''            // Team Deathmatch, unless the package's map.json lists the game types
            // it carries ("gameTypes": "war dm dom"). The row's own game type
            // stays "war" when the list has it, otherwise the first one listed.
            const char *types = map.gameTypes[0] ? map.gameTypes : "war";
            char first[12]{};
            bool listsWar = false;
            for (unsigned at = 0, word = 0; types[at]; ++at)
            {
                if (types[at] == ' ')
                {
                    word = at + 1;
                    continue;
                }
                if (!word && at < sizeof(first) - 1)
                    first[at] = types[at];
                if (at == word && types[at] == 'w' && types[at + 1] == 'a' && types[at + 2] == 'r' &&
                    (!types[at + 3] || types[at + 3] == ' '))
                    listsWar = true;
            }
            Copy(reinterpret_cast<char *>(row.native + 0x40), 32, listsWar ? "war" : first);
            Copy(row.gameTypes, sizeof(row.gameTypes), types);'''),
]
for a, b in pairs:
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)
io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)
os.makedirs('docs/research/79_client', exist_ok=True)
diff = difflib.unified_diff(before.split('\n'), s.split('\n'), 'a/integration/replay/DW_MapAvailability.cpp',
                            'b/integration/replay/DW_MapAvailability.cpp', lineterm='', n=3)
io.open('docs/research/79_client/DW_MapAvailability.cpp.diff', 'w', encoding='utf-8', newline='\n').write('\n'.join(diff) + '\n')
print('ok', 'crlf' if crlf else 'lf')
