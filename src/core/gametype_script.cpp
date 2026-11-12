#include "gametype_script.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace iw8r {

namespace {

std::string lowerStr(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// GSC text with comments and string contents blanked (newlines kept, so line numbers stay).
std::string codeOnly(const std::string& gsc) {
    std::string out = gsc;
    enum { Code, Line, Block, Str } st = Code;
    for (size_t i = 0; i < out.size(); ++i) {
        const char c = gsc[i];
        const char n = i + 1 < gsc.size() ? gsc[i + 1] : '\0';
        switch (st) {
        case Code:
            if (c == '/' && n == '/') st = Line, out[i] = ' ';
            else if (c == '/' && n == '*') st = Block, out[i] = ' ';
            else if (c == '"') st = Str;
            break;
        case Line:
            if (c == '\n') st = Code;
            else out[i] = ' ';
            break;
        case Block:
            if (c == '*' && n == '/') {
                out[i] = out[i + 1] = ' ';
                ++i;
                st = Code;
            } else if (c != '\n') {
                out[i] = ' ';
            }
            break;
        case Str:
            if (c == '\\' && n) {
                out[i] = ' ';
                if (n != '\n') out[i + 1] = ' ';
                ++i;
            } else if (c == '"') {
                st = Code;
            } else if (c != '\n') {
                out[i] = ' ';
            }
            break;
        }
    }
    return out;
}

}  // namespace

bool StockScriptIndex::parse(const std::string& tsv) {
    files_.clear();
    std::istringstream in(tsv);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        const size_t t2 = line.find('\t', t1 + 1);
        const std::string kind = line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
        const Form f = kind == "numbered" ? Form::Numbered : kind == "named" ? Form::Named : Form::Unknown;
        if (f != Form::Unknown) files_[normalizedScriptPath(line.substr(0, t1))] = f;
    }
    return !files_.empty();
}

bool StockScriptIndex::load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (parse(ss.str())) return true;
    if (err) *err = "no stock script in " + path;
    return false;
}

StockScriptIndex::Form StockScriptIndex::form(const std::string& path) const {
    const auto it = files_.find(normalizedScriptPath(path));
    return it == files_.end() ? Form::Unknown : it->second;
}

std::string normalizedScriptPath(const std::string& path) {
    std::string p = lowerStr(path);
    std::replace(p.begin(), p.end(), '\\', '/');
    if (p.size() > 4 && p.compare(p.size() - 4, 4, ".gsc") == 0) p.resize(p.size() - 4);
    return p;
}

std::string gameTypeNameOfScript(const std::string& file) {
    std::string name = file;
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    name = lowerStr(name);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".gsc") == 0) name.resize(name.size() - 4);
    if (name.empty() || name.size() > 11) return {};
    for (const char c : name)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_')) return {};
    return name;
}

std::vector<ScriptFarCall> scriptFarCalls(const std::string& gsc) {
    std::vector<ScriptFarCall> out;
    const std::string code = codeOnly(gsc);
    for (size_t i = code.find("::"); i != std::string::npos; i = code.find("::", i + 2)) {
        // The path before "::": identifier characters and backslashes / slashes; "::name" alone is a local reference.
        size_t b = i;
        while (b > 0 && (identChar(code[b - 1]) || code[b - 1] == '\\' || code[b - 1] == '/' || code[b - 1] == '.')) --b;
        if (b == i) continue;
        size_t e = i + 2;
        while (e < code.size() && identChar(code[e])) ++e;
        if (e == i + 2) continue;
        const std::string path = code.substr(b, i - b);
        if (path.find('\\') == std::string::npos && path.find('/') == std::string::npos) continue;  // not a file path
        ScriptFarCall call;
        call.line = int(std::count(code.begin(), code.begin() + long(b), '\n')) + 1;
        call.path = path;
        call.function = code.substr(i + 2, e - i - 2);
        out.push_back(call);
    }
    return out;
}

std::vector<std::string> checkGameTypeScript(const std::string& gsc, const std::string& name, const StockScriptIndex& index) {
    std::vector<std::string> out;
    if (gameTypeNameOfScript(name) != name || name.empty())
        out.push_back("the game type name '" + name + "' must be 1 to 11 characters of a-z, 0-9 and _ (the script's file name)");
    const std::string own = "scripts/mp/gametypes/" + name;
    if (!name.empty() && index.form(own) != StockScriptIndex::Form::Unknown)
        out.push_back("'" + name + "' is a stock game type: a map cannot replace a stock game type script (research 91); give it a new name");
    // main() defined at the top level.
    const std::string code = codeOnly(gsc);
    bool hasMain = false;
    for (size_t i = code.find("main"); i != std::string::npos && !hasMain; i = code.find("main", i + 4)) {
        if ((i > 0 && identChar(code[i - 1])) || (i + 4 < code.size() && identChar(code[i + 4]))) continue;
        size_t j = i + 4;
        while (j < code.size() && std::isspace(static_cast<unsigned char>(code[j]))) ++j;
        if (j >= code.size() || code[j] != '(') continue;
        const size_t close = code.find(')', j);
        size_t k = close == std::string::npos ? code.size() : close + 1;
        while (k < code.size() && std::isspace(static_cast<unsigned char>(code[k]))) ++k;
        // A definition: at the start of a line and followed by a body.
        size_t ls = i;
        while (ls > 0 && (code[ls - 1] == ' ' || code[ls - 1] == '\t')) --ls;
        hasMain = k < code.size() && code[k] == '{' && (ls == 0 || code[ls - 1] == '\n');
    }
    if (!hasMain) out.push_back("no main(): the game starts a game type script at its main (without it the match drops)");
    for (const ScriptFarCall& c : scriptFarCalls(gsc)) {
        const std::string path = normalizedScriptPath(c.path);
        if (path == own) continue;
        const std::string where = "line " + std::to_string(c.line) + ": " + c.path + "::" + c.function + "()";
        switch (index.form(path)) {
        case StockScriptIndex::Form::Numbered:
            break;
        case StockScriptIndex::Form::Named:
            out.push_back(where + ": that stock script is stored by its name, and a call into it does not link (research 92); "
                                  "the match would end at load. Copy what you need into this script");
            break;
        case StockScriptIndex::Form::Unknown:
            out.push_back(where + ": no stock script of that name is in the always-loaded zones; the match would end at load");
            break;
        }
    }
    return out;
}

std::string gameTypeScriptTemplate(const std::string& name) {
    std::string t = R"GSC(// Game type @NAME@, made from the IW8 Radiant template (docs/research/91 and 92).
// Team kills: every kill of an enemy scores for the killer's team, up to the score limit (scr_@NAME@_scorelimit).
// Rules for this file, checked by every build:
//  - the file name is the game type name (1 to 11 of a-z, 0-9 and _), and it must not be a stock game type;
//  - far calls may go only to stock scripts the game stores by number (scripts\mp\globallogic, gameobjects, gamescore,
//    spawnlogic, utility\game, ...: data/generated/iw8_stock_scripts.tsv). A call into scripts\mp\gametypes\... does
//    not link and would end the match at load.

main()
{
    if ( getdvar( "mapname" ) == "mp_background" )
        return;

    scripts\mp\globallogic::init();
    scripts\mp\globallogic::setupcallbacks();
    gametypes[0] = scripts\mp\utility\game::getgametype();
    scripts\mp\gameobjects::main( gametypes );
    gametype = scripts\mp\utility\game::getgametype();
    scripts\mp\utility\game::registerroundswitchdvar( gametype, 0, 0, 9 );
    scripts\mp\utility\game::registertimelimitdvar( gametype, 600 );
    scripts\mp\utility\game::registerscorelimitdvar( gametype, 50 );
    scripts\mp\utility\game::registerroundlimitdvar( gametype, 1 );
    scripts\mp\utility\game::registerwinlimitdvar( gametype, 1 );
    scripts\mp\utility\game::registernumlivesdvar( gametype, 0 );
    scripts\mp\utility\game::registerhalftimedvar( gametype, 0 );
    // What the stock game types take from scripts\mp\gametypes\common (stored by name, so set here).
    level.dogtagsenabled = 0;
    level.showenemydeathloc = 0;
    level.mpingamelootdrop = 0;
    level.teambased = 1;
    level.onstartgametype = ::onstartgametype;
    level.getspawnpoint = ::getspawnpoint;
    level.onnormaldeath = ::onnormaldeath;
    game["dialog"]["gametype"] = "gametype_tdm";
}

onstartgametype()
{
    if ( !isdefined( game["switchedsides"] ) )
        game["switchedsides"] = 0;

    foreach ( team in level.teamnamelist )
    {
        scripts\mp\utility\game::setobjectivetext( team, &"OBJECTIVES/WAR" );
        scripts\mp\utility\game::setobjectivescoretext( team, &"OBJECTIVES/WAR_SCORE" );
        scripts\mp\utility\game::setobjectivehinttext( team, &"OBJECTIVES/WAR_HINT" );
    }

    initspawns();
}

// The Team Deathmatch spawns of the map: mp_tdm_spawn_allies_start / mp_tdm_spawn_axis_start, mp_tdm_spawn and
// mp_tdm_spawn_secondary.
initspawns()
{
    level.spawnmins = ( 0, 0, 0 );
    level.spawnmaxs = ( 0, 0, 0 );
    scripts\mp\spawnlogic::setactivespawnlogic( "Default", "Crit_Frontline" );
    scripts\mp\spawnlogic::addstartspawnpoints( "mp_tdm_spawn_allies_start" );
    scripts\mp\spawnlogic::addstartspawnpoints( "mp_tdm_spawn_axis_start" );
    scripts\mp\spawnlogic::addspawnpoints( game["attackers"], "mp_tdm_spawn_allies_start" );
    scripts\mp\spawnlogic::addspawnpoints( game["defenders"], "mp_tdm_spawn_axis_start" );
    attackers = scripts\mp\spawnlogic::getspawnpointarray( "mp_tdm_spawn_allies_start" );
    defenders = scripts\mp\spawnlogic::getspawnpointarray( "mp_tdm_spawn_axis_start" );
    scripts\mp\spawnlogic::registerspawnset( "start_attackers", attackers );
    scripts\mp\spawnlogic::registerspawnset( "start_defenders", defenders );
    scripts\mp\spawnlogic::addspawnpoints( "allies", "mp_tdm_spawn" );
    scripts\mp\spawnlogic::addspawnpoints( "axis", "mp_tdm_spawn" );
    scripts\mp\spawnlogic::addspawnpoints( "allies", "mp_tdm_spawn_secondary", 1, 1 );
    scripts\mp\spawnlogic::addspawnpoints( "axis", "mp_tdm_spawn_secondary", 1, 1 );
    normal = scripts\mp\spawnlogic::getspawnpointarray( "mp_tdm_spawn" );
    fallback = scripts\mp\spawnlogic::getspawnpointarray( "mp_tdm_spawn_secondary" );
    scripts\mp\spawnlogic::registerspawnset( "normal", normal );
    scripts\mp\spawnlogic::registerspawnset( "fallback", fallback );
    level.mapcenter = scripts\mp\spawnlogic::findboxcenter( level.spawnmins, level.spawnmaxs );
    setmapcenter( level.mapcenter );
}

getspawnpoint()
{
    team = self.pers["team"];

    if ( scripts\mp\spawnlogic::shoulduseteamstartspawn() )
    {
        if ( team == game["attackers"] )
        {
            scripts\mp\spawnlogic::activatespawnset( "start_attackers", 1 );
            return scripts\mp\spawnlogic::getspawnpoint( self, team, undefined, "start_attackers" );
        }

        scripts\mp\spawnlogic::activatespawnset( "start_defenders", 1 );
        return scripts\mp\spawnlogic::getspawnpoint( self, team, undefined, "start_defenders" );
    }

    scripts\mp\spawnlogic::activatespawnset( "normal", 1 );
    return scripts\mp\spawnlogic::getspawnpoint( self, team, undefined, "fallback" );
}

// victim, attacker, ...: called by the stock damage script for every kill that is not a suicide or a team kill.
onnormaldeath( victim, attacker, lifeid, meansofdeath, objweapon, iskillstreak )
{
    if ( !isdefined( attacker ) || !isplayer( attacker ) || attacker.team == self.team )
        return;

    points = 1;

    if ( isdefined( level.scoremod ) && isdefined( level.scoremod["kill"] ) )
        points = level.scoremod["kill"];

    level scripts\mp\gamescore::giveteamscoreforobjective( attacker.pers["team"], points, 0 );
}
)GSC";
    for (size_t at = t.find("@NAME@"); at != std::string::npos; at = t.find("@NAME@", at + name.size())) t.replace(at, 6, name);
    return t;
}

}  // namespace iw8r
