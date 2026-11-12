# 14. x64-zt verifyzone coverage of MW2019 Replay 1.20 stock zones

Scope: IW8 read path only. Read-only. Nothing was built or dumped. The game folder E:\IW8\Builds\1.20-replay was only read.

## Summary

- Tool: x64-zt `caf5b0f` (`E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe`), build 1.20.4.7623265.
- Zones run: 79. Passed and consumed exactly: 76. Not consumed (exit 1): 3.
- Pools covered: 102 / 117. Not seen: 15. Of those, 6 are null in the IW8 format table (no parser, so no zone can verify them) and 9 are non-null but were absent from every zone tried.
- Assets read across passing zones: 265436 (summed zone header counts; the same type can appear in several zones).
- Not consumed exactly: techsets_mp_frontend7b, mp_frontend7b, mp_frontendc3s5. All three fail at zone open with `xfile version ... is not a Replay 1.20 version`, so they are not Replay 1.20 zones and add nothing to coverage.

## Commands

Environment: `IW8_READ_TRACE` set to `runs/trace_<zone>.txt` under the scratch folder; working directory E:\IW8-Radiant\out\x64zt-verify; stdin from /dev/null; 1200 s timeout per zone.

```bash
IW8_READ_TRACE=E:/IW8-Radiant/out/x64zt-verify/runs/trace_<zone>.txt \
  E:/mw124/x64-zt/build/bin/x64/Release/zonetool.exe -game iw8 -path E:/IW8/Builds/1.20-replay verifyzone <zone> </dev/null
```

Runner: scratchpad `run_zones.sh` (wall time from `date +%s%N`). Summary: scratchpad `summarize.py` (writes coverage.json). Three sweeps: (1) core, common, global, code, weapon, techset and transient zones; (2) srv, eng, ww, iw8_cw, mp_bm_tut, mp_rust, mp_emporium, mp_village2, mp_aniyah_tac and transients; (3) the largest sub-60 MB zone from each name family, which added no new types and ended the sweep. The first smoke run of mp_shipment was not timed; the timed figure is from sweep 1.

Trace format: `A <n> <name>` per asset, where `<n>` is the native pool id from pools.md. For every type seen, the verify log counts (`zone: count` lines) equal the trace counts for that pool id, with 0 mismatches, so the trace index is the pool id in this build.

## Per-zone table

| Zone | Exit | Seconds | Consumed exactly | Assets | Pool types | Verify result |
|---|---:|---:|:---:|---:|---:|---|
| common_mp | 0 | 10.8 | yes | 10479 | 33 | zone "common_mp" passed |
| mp_hideout | 0 | 3.1 | yes | 18642 | 55 | zone "mp_hideout" passed |
| mp_shipment | 0 | 2.4 | yes | 10478 | 58 | zone "mp_shipment" passed |
| mp_backlot2 | 0 | 2.2 | yes | 12784 | 55 | zone "mp_backlot2" passed |
| common_base_mp | 0 | 2.0 | yes | 13581 | 49 | zone "common_base_mp" passed |
| mp_emporium | 0 | 1.7 | yes | 15116 | 49 | zone "mp_emporium" passed |
| mp_aniyah_tac | 0 | 1.6 | yes | 13097 | 50 | zone "mp_aniyah_tac" passed |
| mp_village2 | 0 | 1.4 | yes | 11835 | 50 | zone "mp_village2" passed |
| mp_bm_tut | 0 | 1.1 | yes | 4438 | 41 | zone "mp_bm_tut" passed |
| mp_rust | 0 | 1.0 | yes | 7215 | 54 | zone "mp_rust" passed |
| srv_mp_donetsk | 0 | 0.8 | yes | 5 | 5 | zone "srv_mp_donetsk" passed |
| mp_frontend7b | 1 | 0.7 | no | 0 | 0 |  |
| mp_frontendc3s5 | 1 | 0.7 | no | 0 | 0 |  |
| code_post_gfx | 0 | 0.6 | yes | 6012 | 80 | zone "code_post_gfx" passed |
| global_stream_mp | 0 | 0.6 | yes | 38499 | 28 | zone "global_stream_mp" passed |
| mp_m_king | 0 | 0.6 | yes | 3460 | 49 | zone "mp_m_king" passed |
| techsets_mp_donetsk | 0 | 0.6 | yes | 35606 | 10 | zone "techsets_mp_donetsk" passed |
| techsets_global_stream_mp | 0 | 0.5 | yes | 40650 | 10 | zone "techsets_global_stream_mp" passed |
| mp_br_quarry_cg_ls_tr | 0 | 0.5 | yes | 4857 | 4 | zone "mp_br_quarry_cg_ls_tr" passed |
| common_core_mp | 0 | 0.4 | yes | 2213 | 29 | zone "common_core_mp" passed |
| common_br_mp | 0 | 0.4 | yes | 2749 | 35 | zone "common_br_mp" passed |
| srv_mp_shipment | 0 | 0.4 | yes | 2 | 2 | zone "srv_mp_shipment" passed |
| common_stream_mp | 0 | 0.3 | yes | 2 | 2 | zone "common_stream_mp" passed |
| global_mp | 0 | 0.3 | yes | 7112 | 18 | zone "global_mp" passed |
| iw8_cw_raygun | 0 | 0.3 | yes | 40 | 13 | zone "iw8_cw_raygun" passed |
| techsets_iw8_cw_hinachode_v2 | 0 | 0.3 | yes | 8 | 3 | zone "techsets_iw8_cw_hinachode_v2" passed |
| techsets_mp_shipment | 0 | 0.2 | yes | 3622 | 10 | zone "techsets_mp_shipment" passed |
| dw_custom_operators_a55437fc15603b1f | 0 | 0.2 | yes | 64 | 6 | zone "dw_custom_operators_a55437fc15603b1f" passed |
| mp_donetsk_00032_tr | 0 | 0.2 | yes | 602 | 7 | zone "mp_donetsk_00032_tr" passed |
| global_core_mp | 0 | 0.1 | yes | 28 | 6 | zone "global_core_mp" passed |
| code_pre_gfx | 0 | 0.1 | yes | 43 | 3 | zone "code_pre_gfx" passed |
| dw_weapon_inspect_mp_1f59659a14a316b6 | 0 | 0.1 | yes | 57 | 2 | zone "dw_weapon_inspect_mp_1f59659a14a316b6" passed |
| iw8_cw_raygun_common | 0 | 0.1 | yes | 4 | 2 | zone "iw8_cw_raygun_common" passed |
| techsets_mp_frontend7b | 1 | 0.1 | no | 0 | 0 |  |
| techsets_common | 0 | 0.1 | yes | 1166 | 8 | zone "techsets_common" passed |
| techsets_global | 0 | 0.1 | yes | 9 | 5 | zone "techsets_global" passed |
| techsets_ui | 0 | 0.1 | yes | 5 | 5 | zone "techsets_ui" passed |
| techsets_replay_frontend_131 | 0 | 0.1 | yes | 101 | 4 | zone "techsets_replay_frontend_131" passed |
| mp_av_weapon_vm_me_hatchet01_tr | 0 | 0.1 | yes | 3 | 3 | zone "mp_av_weapon_vm_me_hatchet01_tr" passed |
| mp_av_attachment_vm_ar_falpha_barrel_tr | 0 | 0.1 | yes | 7 | 3 | zone "mp_av_attachment_vm_ar_falpha_barrel_tr" passed |
| eng_dw_bot_styles_mp_dc7a04c95e57c55a | 0 | 0.1 | yes | 0 | 0 | zone "eng_dw_bot_styles_mp_dc7a04c95e57c55a" passed |
| mp_infil_br_donetsk_ending_chopper_tr | 0 | 0.1 | yes | 201 | 4 | zone "mp_infil_br_donetsk_ending_chopper_tr" passed |
| srv_dw_gulag_nav_c67fd4f1d72fd121 | 0 | 0.1 | yes | 0 | 0 | zone "srv_dw_gulag_nav_c67fd4f1d72fd121" passed |
| srv_dw_custom_operators_31f863513486abfc | 0 | 0.1 | yes | 0 | 0 | zone "srv_dw_custom_operators_31f863513486abfc" passed |
| srv_dw_map_images | 0 | 0.1 | yes | 0 | 0 | zone "srv_dw_map_images" passed |
| eng_iw8_cw_raygun | 0 | 0.1 | yes | 2 | 1 | zone "eng_iw8_cw_raygun" passed |
| eng_iw8_cw_hinachode | 0 | 0.1 | yes | 2 | 1 | zone "eng_iw8_cw_hinachode" passed |
| eng_iw8_cw_hinachode_v2 | 0 | 0.1 | yes | 2 | 1 | zone "eng_iw8_cw_hinachode_v2" passed |
| eng_iw8_cw_pocket_blaster | 0 | 0.1 | yes | 2 | 1 | zone "eng_iw8_cw_pocket_blaster" passed |
| iw8_cw_pocket_blaster | 0 | 0.1 | yes | 25 | 13 | zone "iw8_cw_pocket_blaster" passed |
| ww_iw8_cw_raygun | 0 | 0.1 | yes | 0 | 0 | zone "ww_iw8_cw_raygun" passed |
| techsets_dw_map_images | 0 | 0.1 | yes | 6 | 3 | zone "techsets_dw_map_images" passed |
| techsets_mw120r_map_images | 0 | 0.1 | yes | 90 | 3 | zone "techsets_mw120r_map_images" passed |
| techsets_iw8_cw_workbench_integration_rifle | 0 | 0.1 | yes | 8 | 3 | zone "techsets_iw8_cw_workbench_integration_rifle" passed |
| techsets_common_stream_mp | 0 | 0.1 | yes | 2 | 2 | zone "techsets_common_stream_mp" passed |
| mp_bz_body_mp_eastern_fireteam_east_ar_lod0_l15_tr | 0 | 0.1 | yes | 0 | 0 | zone "mp_bz_body_mp_eastern_fireteam_east_ar_lod0_l15_tr" passed |
| mp_hz_head_usmc_ar_lod0_l15_tr | 0 | 0.1 | yes | 0 | 0 | zone "mp_hz_head_usmc_ar_lod0_l15_tr" passed |
| mp_vz_viewhands_base_tr | 0 | 0.1 | yes | 0 | 0 | zone "mp_vz_viewhands_base_tr" passed |
| mp_wy_weapon_vm_ar_falima_tr | 0 | 0.1 | yes | 0 | 0 | zone "mp_wy_weapon_vm_ar_falima_tr" passed |
| mp_av_vm_fullbody_pilot_helicopter_british_tr | 0 | 0.1 | yes | 19 | 3 | zone "mp_av_vm_fullbody_pilot_helicopter_british_tr" passed |
| mp_aw_attachment_vm_lm_mkilo3_receiver_l13_tr | 0 | 0.1 | yes | 18 | 5 | zone "mp_aw_attachment_vm_lm_mkilo3_receiver_l13_tr" passed |
| mp_ay_attachment_vm_lm_pkilo_receiver_v2_tr | 0 | 0.1 | yes | 76 | 4 | zone "mp_ay_attachment_vm_lm_pkilo_receiver_v2_tr" passed |
| mp_az_attachment_wm_ar_falima_reciever_lod1_s2_l22_tr | 0 | 0.1 | yes | 46 | 5 | zone "mp_az_attachment_wm_ar_falima_reciever_lod1_s2_l22_tr" passed |
| mp_bd_body_mp_western_domino_3_1_lod0_l15_tr | 0 | 0.1 | yes | 88 | 6 | zone "mp_bd_body_mp_western_domino_3_1_lod0_l15_tr" passed |
| mp_bz_body_usmc_ar_lod0_l15_tr | 0 | 0.1 | yes | 14 | 7 | zone "mp_bz_body_usmc_ar_lod0_l15_tr" passed |
| mp_hd_head_mp_eastern_minotavr_1_1_blendshape_l15_tr | 0 | 0.1 | yes | 38 | 3 | zone "mp_hd_head_mp_eastern_minotavr_1_1_blendshape_l15_tr" passed |
| mp_hz_head_mp_eastern_fireteam_east_ar_2_l15_tr | 0 | 0.1 | yes | 0 | 0 | zone "mp_hz_head_mp_eastern_fireteam_east_ar_2_l15_tr" passed |
| mp_infil_br_ending_chopper_tr | 0 | 0.1 | yes | 84 | 4 | zone "mp_infil_br_ending_chopper_tr" passed |
| mp_vh_mp_eastern_vm_arms_grinch_5_1_lod0_tr | 0 | 0.1 | yes | 7 | 3 | zone "mp_vh_mp_eastern_vm_arms_grinch_5_1_lod0_tr" passed |
| mp_vz_viewhands_alex_gloves_a_tr | 0 | 0.1 | yes | 0 | 0 | zone "mp_vz_viewhands_alex_gloves_a_tr" passed |
| mp_wv_weapon_vm_lm_dblmg_tr | 0 | 0.1 | yes | 9 | 3 | zone "mp_wv_weapon_vm_lm_dblmg_tr" passed |
| mp_ww_dog_a_mp_execution_lod0_s5_l15_tr | 0 | 0.1 | yes | 14 | 4 | zone "mp_ww_dog_a_mp_execution_lod0_s5_l15_tr" passed |
| mp_wy_weapon_vm_la_rpapa7_tr | 0 | 0.1 | yes | 10 | 3 | zone "mp_wy_weapon_vm_la_rpapa7_tr" passed |
| mp_wz_weapon_wm_la_rpapa7_lod1_l14_tr | 0 | 0.1 | yes | 16 | 5 | zone "mp_wz_weapon_wm_la_rpapa7_lod1_l14_tr" passed |
| mw120r_map_images | 0 | 0.1 | yes | 60 | 2 | zone "mw120r_map_images" passed |
| srv_dw_bot_styles_mp_0155601d9bc14b2c | 0 | 0.1 | yes | 0 | 0 | zone "srv_dw_bot_styles_mp_0155601d9bc14b2c" passed |
| techsets_comms_mp | 0 | 0.1 | yes | 2 | 2 | zone "techsets_comms_mp" passed |
| ui_boot | 0 | 0.1 | yes | 4 | 3 | zone "ui_boot" passed |
| ww_dw_bot_styles_mp_dc7a04c95e57c55a | 0 | 0.1 | yes | 0 | 0 | zone "ww_dw_bot_styles_mp_dc7a04c95e57c55a" passed |

## Per-pool table

| ID | Pool | Zones where seen | Consumed exactly |
|---:|---|---|:---:|
| 0 | physicslibrary | code_post_gfx | yes |
| 1 | physicssfxeventasset | code_post_gfx, global_stream_mp | yes |
| 2 | physicsvfxeventasset | code_post_gfx | yes |
| 3 | physicsasset | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, dw_custom_operators_a55437fc15603b1f (+17 more) | yes |
| 4 | physicsfxpipeline | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, mp_aniyah_tac (+8 more) | yes |
| 5 | physicsfxshape | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, mp_aniyah_tac (+8 more) | yes |
| 6 | physicsdebugdata | none (not seen in any zone tried) | no |
| 7 | xanim | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, dw_weapon_inspect_mp_1f59659a14a316b6 (+11 more) | yes |
| 8 | xmodelsurfs | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, dw_custom_operators_a55437fc15603b1f (+30 more) | yes |
| 9 | xmodel | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, dw_custom_operators_a55437fc15603b1f (+28 more) | yes |
| 10 | mayhem | code_post_gfx | yes |
| 11 | material | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, dw_custom_operators_a55437fc15603b1f (+38 more) | yes |
| 12 | computeshader | code_post_gfx | yes |
| 13 | libshader | code_post_gfx | yes |
| 14 | vertexshader | code_post_gfx, techsets_common, techsets_global_stream_mp, techsets_mp_donetsk, techsets_mp_shipment, techsets_replay_frontend_131 | yes |
| 15 | hullshader | code_post_gfx, techsets_global_stream_mp, techsets_mp_donetsk, techsets_mp_shipment, techsets_replay_frontend_131 | yes |
| 16 | domainshader | code_post_gfx, techsets_global_stream_mp, techsets_mp_donetsk, techsets_mp_shipment, techsets_replay_frontend_131 | yes |
| 17 | pixelshader | code_post_gfx, techsets_common, techsets_global_stream_mp, techsets_mp_donetsk, techsets_mp_shipment, techsets_replay_frontend_131 | yes |
| 18 | techset | code_post_gfx, dw_custom_operators_a55437fc15603b1f, techsets_common, techsets_dw_map_images, techsets_global, techsets_global_stream_mp (+6 more) | yes |
| 19 | image | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, dw_custom_operators_a55437fc15603b1f (+29 more) | yes |
| 20 | soundglobals | code_post_gfx | yes |
| 21 | soundbank | code_post_gfx, common_base_mp, common_br_mp, common_mp, mp_aniyah_tac, mp_backlot2 (+7 more) | yes |
| 22 | soundbanktransient | common_base_mp, common_br_mp, common_core_mp, common_mp, iw8_cw_raygun_common, mp_backlot2 (+4 more) | yes |
| 23 | col_map | mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout, mp_m_king (+3 more) | yes |
| 24 | com_map | mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout, mp_m_king (+3 more) | yes |
| 25 | glass_map | mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout, mp_m_king (+3 more) | yes |
| 26 | aipaths | mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout, mp_m_king (+4 more) | yes |
| 27 | navmesh | code_post_gfx, mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout (+5 more) | yes |
| 28 | tacgraph | mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout, mp_m_king (+4 more) | yes |
| 29 | map_ents | mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout, mp_m_king (+3 more) | yes |
| 30 | fx_map | mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout, mp_m_king (+3 more) | yes |
| 31 | gfx_map | mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout, mp_m_king (+3 more) | yes |
| 32 | gfx_map_trzone | code_post_gfx, mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_donetsk_00032_tr, mp_emporium (+5 more) | yes |
| 33 | iesprofile | none (null in format table, no parser) | no |
| 34 | lightdef | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, mp_aniyah_tac (+8 more) | yes |
| 35 | gradingclut | code_post_gfx, common_base_mp, common_mp, mp_aniyah_tac, mp_backlot2, mp_emporium (+5 more) | yes |
| 36 | ui_map | none (null in format table, no parser) | no |
| 37 | fogspline | common_br_mp, common_mp, mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium (+5 more) | yes |
| 38 | animclass | code_post_gfx | yes |
| 39 | playeranim | none (not seen in any zone tried) | no |
| 40 | gesture | code_post_gfx, common_base_mp, common_br_mp, common_core_mp | yes |
| 41 | localize | eng_iw8_cw_hinachode, eng_iw8_cw_hinachode_v2, eng_iw8_cw_pocket_blaster, eng_iw8_cw_raygun | yes |
| 42 | attachment | code_post_gfx, common_base_mp, global_stream_mp | yes |
| 43 | weapon | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, global_stream_mp, iw8_cw_pocket_blaster (+6 more) | yes |
| 44 | vfx | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, global_stream_mp (+9 more) | yes |
| 45 | impactfx | code_post_gfx, mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout (+4 more) | yes |
| 46 | surfacefx | mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout, mp_m_king (+3 more) | yes |
| 47 | aitype | none (null in format table, no parser) | no |
| 48 | mptype | none (null in format table, no parser) | no |
| 49 | character | none (null in format table, no parser) | no |
| 50 | xmodelalias | none (null in format table, no parser) | no |
| 51 | rawfile | code_post_gfx, code_pre_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp (+24 more) | yes |
| 52 | scriptfile | code_post_gfx, common_base_mp, global_mp, mp_aniyah_tac, mp_backlot2, mp_bm_tut (+6 more) | yes |
| 53 | scriptdebugdata | none (not seen in any zone tried) | no |
| 54 | stringtable | code_post_gfx, common_base_mp, common_br_mp, global_mp, iw8_cw_pocket_blaster, iw8_cw_raygun (+9 more) | yes |
| 55 | leaderboarddef | code_post_gfx | yes |
| 56 | virtualleaderboarddef | code_post_gfx | yes |
| 57 | ddl | code_post_gfx | yes |
| 58 | tracer | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, global_stream_mp (+2 more) | yes |
| 59 | vehicle | code_post_gfx, common_base_mp, common_br_mp, mp_aniyah_tac, mp_backlot2, mp_emporium (+4 more) | yes |
| 60 | addon_map_ents | none (not seen in any zone tried) | no |
| 61 | netconststrings | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, dw_weapon_inspect_mp_1f59659a14a316b6 (+14 more) | yes |
| 62 | luafile | code_post_gfx | yes |
| 63 | scriptable | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, global_core_mp (+11 more) | yes |
| 64 | equipsndtable | common_mp | yes |
| 65 | vectorfield | code_post_gfx, common_base_mp, common_core_mp, mp_aniyah_tac, mp_backlot2, mp_bm_tut (+6 more) | yes |
| 66 | particlesimanimation | code_post_gfx | yes |
| 67 | streaminginfo | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, global_mp (+13 more) | yes |
| 68 | laser | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, global_stream_mp | yes |
| 69 | ttf | code_post_gfx | yes |
| 70 | suit | code_post_gfx, common_base_mp | yes |
| 71 | suitanimpackage | code_post_gfx, common_base_mp | yes |
| 72 | camera | code_post_gfx, common_base_mp, common_br_mp, common_mp, mp_aniyah_tac, mp_backlot2 (+5 more) | yes |
| 73 | hudoutline | code_post_gfx, common_br_mp, common_mp, mp_backlot2, mp_hideout, mp_rust (+1 more) | yes |
| 74 | spaceshiptarget | none (not seen in any zone tried) | no |
| 75 | rumble | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, global_stream_mp, iw8_cw_pocket_blaster (+7 more) | yes |
| 76 | rumblegraph | code_post_gfx, common_base_mp, global_stream_mp, iw8_cw_pocket_blaster, iw8_cw_raygun, mp_aniyah_tac (+4 more) | yes |
| 77 | animpkg | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, global_stream_mp (+5 more) | yes |
| 78 | sfxpkg | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, global_stream_mp (+6 more) | yes |
| 79 | vfxpkg | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, global_stream_mp (+5 more) | yes |
| 80 | footstepvfx | common_base_mp, global_stream_mp, mp_backlot2, mp_hideout, mp_rust, mp_shipment | yes |
| 81 | behaviortree | mp_shipment | yes |
| 82 | aianimset | code_post_gfx, common_base_mp, mp_aniyah_tac, mp_backlot2, mp_emporium, mp_hideout (+2 more) | yes |
| 83 | aiasm | code_post_gfx, common_base_mp, mp_shipment | yes |
| 84 | proceduralbones | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, global_stream_mp (+9 more) | yes |
| 85 | dynamicbones | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, global_stream_mp, mp_aniyah_tac (+11 more) | yes |
| 86 | reticle | code_post_gfx, common_base_mp, global_stream_mp | yes |
| 87 | xanimcurve | common_base_mp, global_stream_mp, mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_hideout (+2 more) | yes |
| 88 | coverselector | global_mp | yes |
| 89 | enemyselector | global_mp | yes |
| 90 | clientcharacter | code_post_gfx, global_core_mp | yes |
| 91 | clothasset | code_post_gfx, global_stream_mp | yes |
| 92 | cinematicmotion | code_post_gfx, common_base_mp, common_mp, mp_aniyah_tac, mp_backlot2, mp_emporium (+2 more) | yes |
| 93 | accessory | global_mp | yes |
| 94 | locdmgtable | code_post_gfx, global_mp | yes |
| 95 | bulletpenetration | code_post_gfx, global_mp | yes |
| 96 | scriptbundle | none (not seen in any zone tried) | no |
| 97 | blendspace2d | code_post_gfx, common_base_mp, common_mp | yes |
| 98 | xcam | code_post_gfx, common_base_mp | yes |
| 99 | camo | code_post_gfx, global_mp | yes |
| 100 | xcompositemodel | common_br_mp, common_core_mp, mp_aniyah_tac, mp_backlot2, mp_emporium, mp_hideout (+3 more) | yes |
| 101 | xmodeldetailcollision | code_post_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp, global_stream_mp (+13 more) | yes |
| 102 | streamkey | code_post_gfx, common_base_mp, common_br_mp, common_mp, mp_aniyah_tac, mp_backlot2 (+7 more) | yes |
| 103 | streamtreeoverride | none (not seen in any zone tried) | no |
| 104 | keyvaluepairs | code_post_gfx, code_pre_gfx, common_base_mp, common_br_mp, common_core_mp, common_mp (+24 more) | yes |
| 105 | stterrain | mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout, mp_m_king (+3 more) | yes |
| 106 | nativescriptpatch | none (not seen in any zone tried) | no |
| 107 | collisiontile | code_post_gfx, mp_br_quarry_cg_ls_tr | yes |
| 108 | execution | code_post_gfx, common_base_mp | yes |
| 109 | carryobject | code_post_gfx | yes |
| 110 | soundbanklist | none (not seen in any zone tried) | no |
| 111 | decalvolumematerial | code_post_gfx, common_base_mp, common_mp, global_mp, mp_aniyah_tac, mp_backlot2 (+11 more) | yes |
| 112 | decalvolumemask | code_post_gfx, mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_emporium, mp_hideout (+4 more) | yes |
| 113 | dynentitylist | code_post_gfx, mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_donetsk_00032_tr, mp_emporium (+5 more) | yes |
| 114 | fx_map_trzone | code_post_gfx, mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_donetsk_00032_tr, mp_emporium (+5 more) | yes |
| 115 | dlogschema | code_pre_gfx | yes |
| 116 | edgelist | code_post_gfx, mp_aniyah_tac, mp_backlot2, mp_bm_tut, mp_br_quarry_cg_ls_tr, mp_emporium (+5 more) | yes |

## Pools never seen

- 6 physicsdebugdata: non-null but absent from every zone tried
- 33 iesprofile: null in IW8 format table (asset_streams.cpp), cannot be verified
- 36 ui_map: null in IW8 format table (asset_streams.cpp), cannot be verified
- 39 playeranim: non-null but absent from every zone tried
- 47 aitype: null in IW8 format table (asset_streams.cpp), cannot be verified
- 48 mptype: null in IW8 format table (asset_streams.cpp), cannot be verified
- 49 character: null in IW8 format table (asset_streams.cpp), cannot be verified
- 50 xmodelalias: null in IW8 format table (asset_streams.cpp), cannot be verified
- 53 scriptdebugdata: non-null but absent from every zone tried
- 60 addon_map_ents: non-null but absent from every zone tried
- 74 spaceshiptarget: non-null but absent from every zone tried
- 96 scriptbundle: non-null but absent from every zone tried
- 103 streamtreeoverride: non-null but absent from every zone tried
- 106 nativescriptpatch: non-null but absent from every zone tried
- 110 soundbanklist: non-null but absent from every zone tried

## Name mapping

x64-zt log names match the ACTS pool names in pools.md for all 102 seen pools (0 differences). No renames needed.

## Caveats

- Coverage is limited to the zones tried. A pool absent here may still be present in an untried zone.
- consumed exactly means exit 0, `zone ... passed`, and `all bytes read` in the verify log. It says nothing about a rebuild round trip (IW8_REWRITE or buildzone), which doc 08 section 2 says is needed per type.
- The three frontend zones exit 1 at open, so they say nothing about their contents.
