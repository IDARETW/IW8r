// IW8 Radiant dev showcase 2: map script (worldspawn iw8r_script). Written by tools/make_dev_showcase2.py.
// Station messages use the Script dock's "Trigger message" code with iw8r_trigger() in place of getent(): the build's
// own trigger test notifies that object (the game's trigger entities did not fire in built maps, fix queue F.1).
user_main()
{
    level endon("game_ended");
    wait 1;
    level thread show2_station("s01_world", "^301^7  SUN SKY FOG FILM GRADE");
    level thread show2_station("s02_spawns", "^302^7  SPAWNS TDM + DM");
    level thread show2_station("s03_omni", "^303^7  OMNI LIGHT");
    level thread show2_station("s04_omni_colour", "^304^7  OMNI RED + BLUE");
    level thread show2_station("s05_spot", "^305^7  SPOT BY ANGLES");
    level thread show2_station("s06_spot_target", "^306^7  SPOT WITH TARGET");
    level thread show2_station("s07_spot_ies", "^307^7  SPOT PLAIN / IES PROFILE");
    level thread show2_station("s08_spot_shadow", "^308^7  SPOT SHADOW CASTER");
    level thread show2_station("s09_light_keys", "^309^7  LIGHT TUNING KEYS");
    level thread show2_station("s10_spot_wide", "^310^7  WIDE SPOT 140 / 160");
    level thread show2_station("s11_probes", "^311^7  REFLECTION PROBE PAIR");
    level thread show2_station("s12_volume", "^312^7  VOLUMETRIC BOX");
    level thread show2_station("s13_detail", "^313^7  STRUCTURAL / DETAIL");
    level thread show2_station("s14_flags", "^314^7  NON-COLLIDING / WEAPON CLIP");
    level thread show2_station("s15_clip", "^315^7  CLIP PLAYER WEAPON NOSIGHT");
    level thread show2_station("s16_slopes", "^316^7  RAMP + STAIRS");
    level thread show2_station("s17_patch_curve", "^317^7  PATCH CURVED BOTH SIDES");
    level thread show2_station("s18_patch_terrain", "^318^7  PATCH TERRAIN");
    level thread show2_station("s19_glass", "^319^7  GLASS PANE");
    level thread show2_station("s20_ladder", "^320^7  LADDER");
    level thread show2_station("s21_mantle", "^321^7  MANTLE 40 / 56 / 72");
    level thread show2_station("s22_water", "^322^7  WATER VOLUME");
    level thread show2_station("s23_prefabs", "^323^7  PREFABS");
    level thread show2_station("s24_decal", "^324^7  DECAL (WAITING)");
    level thread show2_station("s25_stock_mat", "^325^7  STOCK MATERIALS");
    level thread show2_station("s26_stock_cutout", "^326^7  STOCK CUT-OUT");
    level thread show2_station("s27_custom", "^327^7  CUSTOM MATERIAL");
    level thread show2_station("s28_cutout", "^328^7  CUSTOM ALPHA TEST");
    level thread show2_station("s29_blend", "^329^7  ALPHA BLEND");
    level thread show2_station("s30_add", "^330^7  ADDITIVE");
    level thread show2_station("s31_multiply", "^331^7  MULTIPLY");
    level thread show2_station("s32_unlit", "^332^7  UNLIT");
    level thread show2_station("s33_emissive", "^333^7  EMISSIVE");
    level thread show2_station("s34_tint", "^334^7  PLAIN / TINTED");
    level thread show2_station("s35_layer", "^335^7  DECAL LAYER ON A WALL");
    level thread show2_station("s36_tool_mats", "^336^7  CAULK NODRAW HINT SKIP");
    level thread show2_station("s37_static", "^337^7  STOCK STATIC MODELS");
    level thread show2_station("s38_scaled", "^338^7  SCALE 0.5 / 1 / 1.5");
    level thread show2_station("s39_shape", "^339^7  SHAPE MODELS");
    level thread show2_station("s40_custom_model", "^340^7  CUSTOM OBJ MODEL");
    level thread show2_station("s41_model_material", "^341^7  MATERIAL ON A MODEL");
    level thread show2_station("s42_model_slot", "^342^7  ONE SLOT REPLACED");
    level thread show2_station("s43_dynamic", "^343^7  DYNAMIC PROPS");
    level thread show2_station("s44_live", "^344^7  LIVE SOLID / NO COLLISION");
    level thread show2_station("s45_model_flags", "^345^7  LOD / NO SHADOW / GROUND LIT");
    level thread show2_station("s46_anim", "^346^7  ANIMATED FAN");
    level thread show2_station("s47_foliage", "^347^7  FOLIAGE");
    level thread show2_station("s48_rotation", "^348^7  YAW / PITCH / ROLL");
    level thread show2_station("s49_spin", "^349^7  SPINNER YAW / ROLL");
    level thread show2_station("s50_bob", "^350^7  BOBBER");
    level thread show2_station("s51_brush_movers", "^351^7  BRUSH SPIN / BOB");
    level thread show2_station("s52_lift", "^352^7  LIFT");
    level thread show2_station("s53_door_slide", "^353^7  SLIDING DOOR (USE)");
    level thread show2_station("s54_door_rotate", "^354^7  ROTATING DOOR (TOUCH)");
    level thread show2_station("s55_door_hinge", "^355^7  HINGED DOOR (USE)");
    level thread show2_station("s56_teleport", "^356^7  TELEPORTER");
    level thread show2_station("s57_jump", "^357^7  JUMP PAD");
    level thread show2_station("s58_script", "^358^7  MAP SCRIPT BLINK");
    level thread show2_blink("s58_blink");
    level thread show2_station("s59_hud", "^359^7  HUD MESSAGE (USE)");
    level thread show2_hud("s59_hud_trig", "HUD message from the map script", 3);
    level thread show2_station("s60_vision", "^360^7  VISION VOLUME");
    level thread show2_station("s61_hurt", "^361^7  HURT TRIGGER");
    level thread show2_station("s62_damage", "^362^7  DAMAGE ZONE");
    level thread show2_station("s63_use", "^363^7  USE TRIGGER");
    level thread show2_message("s63_use_trig", "trigger_use fired");
    level thread show2_station("s64_touch", "^364^7  TOUCH TRIGGER");
    level thread show2_message("s64_touch_trig", "trigger_multiple fired");
    level thread show2_station("s65_radius", "^365^7  RADIUS TRIGGER");
    level thread show2_message("s65_radius_trig", "trigger_radius fired");
    level thread show2_station("s66_trigger_classes", "^366^7  USE TOUCH / DAMAGE");
    level thread show2_message("s66_use_touch", "trigger_use_touch fired");
    level thread show2_message("s66_damage", "trigger_damage fired");
    level thread show2_station("s67_fx_loop", "^367^7  STOCK FX FIRE + SMOKE");
    level thread show2_station("s68_fx_timed", "^368^7  STOCK FX ON A TIMER");
    level thread show2_station("s69_fx_sound", "^369^7  FX + SOUND + DAMAGE");
    level thread show2_station("s70_fx_authored", "^370^7  AUTHORED EFFECT");
    level thread show2_station("s71_snd_loop", "^371^7  STOCK SOUND LOOP");
    level thread show2_station("s72_snd_shot", "^372^7  STOCK SOUND EVERY 4 S");
    level thread show2_station("s73_snd_custom", "^373^7  OWN SOUNDS HUM + CHIRP");
    level thread show2_station("s74_reverb", "^374^7  REVERB ZONE");
    level thread show2_station("s75_ambient", "^375^7  AMBIENT ZONE");
    level thread show2_station("s76_nodes", "^376^7  PATH NODES");
    level thread show2_station("s77_traverse", "^377^7  TRAVERSAL JUMP DOWN");
    level thread show2_station("s78_cameras", "^378^7  MATCH CAMERAS");
    level thread show2_station("s79_minimap", "^379^7  MINIMAP CORNERS");
    level thread show2_station("s80_killstreak", "^380^7  KILLSTREAK MARKERS");
    level thread show2_station("s81_airdrop", "^381^7  AIRDROP CRATE");
    level thread show2_station("s82_callouts", "^382^7  CALLOUT / BATTLE CHATTER");
    level thread show2_station("s83_bounds", "^383^7  BOUNDARY TRIGGERS");
    level thread show2_station("s84_dom", "^384^7  DOMINATION FLAGS A B C");
    level thread show2_station("s85_sd", "^385^7  SEARCH AND DESTROY");
    level thread show2_station("s86_ctf", "^386^7  CAPTURE THE FLAG");
    level thread show2_station("s87_other_spawns", "^387^7  OTHER MODE SPAWNS");
    level thread show2_station("s88_hq", "^388^7  HEADQUARTERS RADIO");
    level thread show2_station("s89_grind", "^389^7  GRIND BANK");
    level thread show2_station("s90_hardpoint", "^390^7  HARDPOINT ZONES 1-5");
    level thread show2_station("s91_cyber", "^391^7  CYBER ATTACK");
    level thread show2_station("s92_dd", "^392^7  DEMOLITION SITES");
    level thread show2_station("s93_goals", "^393^7  FLAG GOALS + GROUND ZONE");
}

// Prints `text` when a player walks into the station trigger `name`, once per visit.
show2_station(name, text)
{
    level endon("game_ended");
    trig = iw8r_trigger(name);
    if (!isdefined(trig))
        return;
    for (;;)
    {
        trig waittill("trigger", player);
        if (!isdefined(player) || !isplayer(player))
            continue;
        now = gettime();
        if (!isdefined(player.show2_name) || player.show2_name != name || now - player.show2_time > 1500)
            player iprintlnbold(text);
        player.show2_name = name;
        player.show2_time = now;
    }
}

// Prints `text` each time the trigger `name` fires (at most every 2 seconds).
show2_message(name, text)
{
    level endon("game_ended");
    trig = iw8r_trigger(name);
    if (!isdefined(trig))
        return;
    for (;;)
    {
        trig waittill("trigger", player);
        if (isdefined(player) && isplayer(player))
            player iprintlnbold(text);
        wait 2;
    }
}

// The Script dock's "HUD message" code, started by a trigger.
show2_hud(name, text, seconds)
{
    level endon("game_ended");
    trig = iw8r_trigger(name);
    if (!isdefined(trig))
        return;
    for (;;)
    {
        trig waittill("trigger", player);
        msg = newhudelem();
        msg.x = 0;
        msg.y = -60;
        msg.alignx = "center";
        msg.aligny = "middle";
        msg.horzalign = "center";
        msg.vertalign = "middle";
        msg.fontscale = 1.5;
        msg.alpha = 1;
        msg settext(text);
        wait seconds;
        msg destroy();
        wait 1;
    }
}

// Hides the model for 1 second every 3 seconds.
show2_blink(name)
{
    level endon("game_ended");
    ent = getent(name, "targetname");
    if (!isdefined(ent))
        return;
    for (;;)
    {
        wait 2;
        ent hide();
        wait 1;
        ent show();
    }
}
