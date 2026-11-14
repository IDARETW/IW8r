import io

# Converter (x64-zt worktree): a map's own localized strings (IW8 Radiant ROADMAP 4.27, research 36 section 8.2.2).
# The extraction may hold localize/strings.tsv: one "KEY<TAB>text" per line (in the text, backslash-n is a line break
# and two backslashes are one). Each becomes a LocalizeEntry in eng_<map>, beside the map-intro chyron, so the map
# script can use &"KEY" (hint strings, HUD labels).

ROOT = 'C:/zsw-iw8r/scriptables/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


patch('src/convert/iw3_iw8/iw3_fastfile.h', '''    std::string chyronClock;
    std::array<std::string, 2> chyronUnits;
''', '''    std::string chyronClock;
    std::array<std::string, 2> chyronUnits;
    // The map's own localized strings (the extraction's localize/strings.tsv): key and text, written into eng_<map>.
    std::vector<std::pair<std::string, std::string>> mapStrings;
''')
F = 'src/convert/iw3_iw8/iw3_fastfile.cpp'
patch(F, '''    , chyronClock(std::move(other.chyronClock))
    , chyronUnits(std::move(other.chyronUnits))
{''', '''    , chyronClock(std::move(other.chyronClock))
    , chyronUnits(std::move(other.chyronUnits))
    , mapStrings(std::move(other.mapStrings))
{''')
patch(F, '''        chyronClock = std::move(other.chyronClock);
        chyronUnits = std::move(other.chyronUnits);
        other.scratch.clear();''', '''        chyronClock = std::move(other.chyronClock);
        chyronUnits = std::move(other.chyronUnits);
        mapStrings = std::move(other.mapStrings);
        other.scratch.clear();''')
patch(F, '''Json ReadSunFlare(const std::filesystem::path &exportRoot, const std::string &sourceMap)
{''', '''// The map's own localized strings: localize/strings.tsv, "KEY<TAB>text" per line. A key is letters, digits, '_' and
// '/'; in the text "\\\\n" is a line break and "\\\\\\\\" one backslash. A line that does not fit is left out with a warning.
std::vector<std::pair<std::string, std::string>> ReadMapStrings(const std::filesystem::path &exportRoot)
{
    std::vector<std::pair<std::string, std::string>> result;
    std::ifstream input(exportRoot / "localize" / "strings.tsv", std::ios::binary);
    if (!input)
        return result;
    std::set<std::string> seen;
    std::string line;
    std::size_t number = 0;
    while (std::getline(input, line))
    {
        ++number;
        if (!line.empty() && line.back() == '\\r')
            line.pop_back();
        if (line.empty())
            continue;
        const auto tab = line.find('\\t');
        const std::string key = tab == std::string::npos ? line : line.substr(0, tab);
        const bool keyOk = !key.empty() && key.size() <= 120 && std::ranges::all_of(key, [](const unsigned char value) {
            return std::isalnum(value) || value == '_' || value == '/';
        });
        if (tab == std::string::npos || !keyOk || !seen.insert(key).second)
        {
            zt::warn("iw3: localize/strings.tsv line %zu left out (no tab, a key outside A-Z 0-9 _ /, or a key twice)", number);
            continue;
        }
        std::string text;
        for (std::size_t i = tab + 1; i < line.size(); ++i)
        {
            if (line[i] == '\\\\' && i + 1 < line.size() && (line[i + 1] == 'n' || line[i + 1] == '\\\\'))
            {
                text.push_back(line[i + 1] == 'n' ? '\\n' : '\\\\');
                ++i;
            }
            else
                text.push_back(line[i]);
        }
        result.emplace_back(key, std::move(text));
    }
    if (!result.empty())
        zt::info("iw3: %zu localized string(s) of the map (localize/strings.tsv)", result.size());
    return result;
}

Json ReadSunFlare(const std::filesystem::path &exportRoot, const std::string &sourceMap)
{''')
patch(F, '''    result.fxEffects = ReadPreparedFx(exportRoot);''', '''    result.mapStrings = ReadMapStrings(exportRoot);
    result.fxEffects = ReadPreparedFx(exportRoot);''')
C = 'src/convert/iw3_iw8/convert.cpp'
patch(C, '''    iw8::ZoneWriter worldwide;
    if (stockCopier)
    {
        stockCopier->RegisterEnglishImages(english);''', '''    if (prepared && !prepared->mapStrings.empty())
    {
        // The map's own text (IW8 Radiant: hint strings and labels for the map script, &"KEY").
        for (const auto &[key, text] : prepared->mapStrings)
            iw8::localize::Register(english, key, text);
        info("build-map: eng_%s: %zu localized string(s) of the map", map.c_str(), prepared->mapStrings.size());
    }
    iw8::ZoneWriter worldwide;
    if (stockCopier)
    {
        stockCopier->RegisterEnglishImages(english);''')
print('ok')
