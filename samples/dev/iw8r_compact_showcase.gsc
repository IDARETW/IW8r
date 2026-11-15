// IW8 Radiant dev showcase map script (worldspawn iw8r_script): the live script_models of the
// live_script_models_and_misc_model row, driven the way stock map scripts drive theirs.
user_main()
{
    level thread showcase_spin("scripted_0");
    level thread showcase_bob("scripted_1");
    level thread showcase_blink("scripted_2");
}

showcase_spin(name)
{
    ent = getent(name, "targetname");
    if (!isdefined(ent))
        return;
    for (;;)
    {
        ent rotateyaw(360, 4);
        wait 4;
    }
}

showcase_bob(name)
{
    ent = getent(name, "targetname");
    if (!isdefined(ent))
        return;
    for (;;)
    {
        ent movez(32, 1.5, 0.5, 0.5);
        wait 1.5;
        ent movez(-32, 1.5, 0.5, 0.5);
        wait 1.5;
    }
}

showcase_blink(name)
{
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
