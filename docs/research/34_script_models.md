# 34. Script models in Replay 1.20 and through the x64-zt route

Date: 2026-11-09. Evidence: ACTS `map_ents` dumps of six stock MP zones (mp_shipment, mp_backlot2, mp_hideout,
mp_aniyah_tac, mp_emporium, mp_rust), decoded with `data/generated/iw8_canonical_keys.tsv`; x64-zt source
(`src/convert/iw3_iw8/iw3_fastfile.cpp`); the built dev showcase (`samples/dev/iw8r_dev_showcase.map`, map id
`mp_iw8r_dev_sho`) inspected with ACTS.

## 1. What stock maps use script_model for

- Few: 25-52 script_models per map (205 in the six maps), against 611-3234 entities per map. Decor is not a
  script_model: stock props are world static models (mp_shipment: about 9,500 placed instances, doc 30).
- Almost all are live gameplay or scripted objects:
  - `script_gameobjectname` on 171 of 205 (bombzone, sd, ctf, cyber, dom...), `script_exploder` on 92, `target` on 81.
  - `targetname` on 202 of 205; `spawnflags` on 121 (values 0 and 4; meaning of 4 UNKNOWN).
  - Models: S&D crates (`sd_equipment_bombsite_crate` + `_d`), care packages, EMP / briefcase bombs, CTF flags
    (`highway_flag0`), a few doors marked `script_noteworthy "infil_delete"`.
- Every script_model carries `footprint_mask_group "default"` (205 of 205). `receivevolumedecals`, `modelscale`,
  `lgvmerge` appear occasionally.

## 2. What x64-zt does with a script_model (`TakeUnscriptedScriptModels`, iw3_fastfile.cpp ~4339-4400)

- It stays an entity (a live script_model) when it has `script_gameobjectname` or `script_exploder`, when another
  entity's `target` names its `targetname`, when the converted map script uses its targetname, or for a few special
  targetnames (exploder, destructible, ...). A model that draws only tool surfaces also stays.
- Every other script_model is promoted to a static model (`mw120r/<id>/smodel_N`), with a PhysicsAsset from its
  collision LOD or visual mesh. Contents 0xB180 stay outside the player mask (players pass props, as in IW3); in the
  owner's test bullets passed too, hence the editor's solid collision boxes (CompileOptions::modelCollisionBoxes).
- A live script_model's xmodel is written under its own name and gets a keyframed PhysicsAsset (verified in the
  showcase build: `iw8r_stock_military_hq_crate_01_proxy`, `iw8r_dev_crate`, ...).
- The converter also emits one modelless script_model of its own (seen in every editor build).

## 3. What the editor does now

- Models browser Place and File > Import Model create a `misc_model` (static decor), matching stock maps.
- Model collision boxes follow the converter's rule exactly: every model that ends up static gets a box; live
  script_models (gameobject, exploder, targeted) keep their own keyframed physics and get none.
- The compiler adds `footprint_mask_group "default"` to script_models that lack it.
- Check Map flags a script_model that will be built as a static prop (no gameobject, exploder or targeting entity).
- The showcase has both kinds: a `stock_models_misc_model` row and a `live_script_models_and_misc_model` row whose
  script_models are each targeted by a script_origin.

Verified in the built showcase package: 4 live script_models with model keys and `footprint_mask_group`, their
xmodels in the zone by name, 20 static models, 93 collision hulls (20 model boxes).

## 4. UNKNOWN

- `spawnflags 4` on stock script_models (an IW8 entity flag; not decoded).
- Whether live script_models block players in game (keyframed body contents), to check on the showcase.
