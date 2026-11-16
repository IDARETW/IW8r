// Game type teamkills, made from the IW8 Radiant template (docs/research/91 and 92).
// Team kills: every kill of an enemy scores for the killer's team, up to the score limit (scr_teamkills_scorelimit).
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
