// Map script for mp_iw8r_scr1 (reproduction of the owner's spin script, 2026-11-11).

user_main()
{
    level endon("game_ended");
    wait 1;
    level thread iw8r_spin("tm/building_horse_stall_wood_beam_128_01", 4);
}

// Spinner: every entity named `name` turns one full circle every `seconds`. In user_main():
//     level thread iw8r_spin("fan1", 4);
// iw8r_spin(name, seconds)
//{
    level endon("game_ended");
    ents = getentarray(name, "tm/building_horse_stall_wood_beam_128_01");
    for (;;)
    {
        foreach (ent in ents)
            ent rotateyaw(360, seconds);
        wait seconds;
    }
}
