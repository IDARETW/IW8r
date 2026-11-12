// Map script for iw8r_dev_showcase (IW8 Radiant).
// This file is appended to the generated map script; the build starts user_main() once the level has loaded.
// Entities are found by their targetname. Script > Insert adds ready-made behaviours below.

user_main()
{
    level endon("game_ended");
    wait 1;
    // Example: a script_brushmodel with targetname "door1" that opens when a trigger_multiple
    // with targetname "door1_trigger" is touched (Script > Insert > Sliding door):
    // level thread iw8r_door("door1", "door1_trigger", (0, 0, 96), 1.5, 3);
}

// Spinner: every entity named `name` turns one full circle every `seconds`. In user_main():
//     level thread iw8r_spin("fan1", 4);
// iw8r_spin(name, seconds)
//{
//    level endon("game_ended");
//    ents = getentarray(name, "tm/building_horse_stall_wood_beam_128_01");
//    for (;;)
//    {
//        foreach (ent in ents)
//            ent rotateyaw(360, seconds);
//        wait seconds;
//    }
//}
