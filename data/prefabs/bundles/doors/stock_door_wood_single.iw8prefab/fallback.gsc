// Stock door from stock parts: the script of the prefab bundle stock_door_wood_single (fallback contents).
// Every function name starts with prefab_; the build renames that prefix, so two prefab kinds never clash.
//
// The numbers are the stock door's own (scriptable_door_wooden_panel_03_painted_mp, docs/research/83 section A.2):
// it opens 90 degrees away from the player in 0.8 s (0.1 s speeding up, 0.5 s slowing down), closes in 0.4 s,
// can be used within 125 units of a point 45 units along the leaf and 35 up, and plays the stock door sounds.
// Not matched: sprinting into the door to bash it open, peeking, and the half open states.

prefab_main(inst)
{
    wait 0.05;
    door = getent(iw8r_pname(inst, "door"), "targetname");
    if (!isdefined(door))
        return;
    usePoint = inst.origin + anglestoforward(inst.angles) * 45 + (0, 0, 35);
    spot = getent(iw8r_pname(inst, "door_use"), "targetname");
    if (isdefined(spot))
        usePoint = spot.origin;
    // The side a player stands on: the leaf runs along the placement's forward direction from the hinge.
    right = anglestoright(inst.angles);
    opened = 0;
    door makeusable();
    door setuserange(125);
    door sethintstring("Open Door");
    level thread prefab_watch_use(door);
    level thread prefab_watch_players(door, usePoint);
    for (;;)
    {
        door waittill("iw8r_door_use", user);
        if (opened == 0)
        {
            // Away from the user: a positive yaw swings the leaf to the left.
            dir = 1;
            if (isdefined(user) && vectordot(user.origin - inst.origin, right) < 0)
                dir = -1;
            door rotateyaw(90 * dir, 0.8, 0.1, 0.5);
            door playsound("scrpt_door_wood_single_open");
            wait 0.8;
            opened = dir;
            door sethintstring("Close Door");
        }
        else
        {
            door rotateyaw(0 - 90 * opened, 0.4);
            door playsound("scrpt_door_wood_single_close");
            wait 0.4;
            opened = 0;
            door sethintstring("Open Door");
        }
    }
}

// The game's own use prompt on the door model, when it fires.
prefab_watch_use(door)
{
    for (;;)
    {
        door waittill("trigger", user);
        door notify("iw8r_door_use", user);
    }
}

// The same from the script side, like the map's trigger volumes: a living player within reach who presses Use.
// One press is one use; a use that arrives while the door moves is dropped.
prefab_watch_players(door, usePoint)
{
    for (;;)
    {
        wait 0.05;
        if (!isdefined(level.players))
            continue;
        foreach (p in level.players)
        {
            if (!isdefined(p) || !isalive(p))
                continue;
            if (!p usebuttonpressed())
            {
                p.iw8r_door_held = 0;
                continue;
            }
            if (distancesquared(p.origin + (0, 0, 35), usePoint) > 125 * 125)
                continue;
            if (isdefined(p.iw8r_door_held) && p.iw8r_door_held)
                continue;
            p.iw8r_door_held = 1;
            door notify("iw8r_door_use", p);
        }
    }
}
