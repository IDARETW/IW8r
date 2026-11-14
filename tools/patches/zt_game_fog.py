import io
import sys

# Converter: a built map can use the game's own fog and volumetric air (IW8 Radiant research 126).
#   1. Names for veilStrength and the volumetric* vision fields (they worked only as raw tokens).
#   2. fogSunDir from the map's sun in both styles (the atmospheric fog's sun glow needs it; nothing reads it while the
#      atmospheric fog is off).
#   3. When the vision overrides turn the atmospheric fog on (fogAtmosphericScattering 1), the game fogs every opaque
#      pixel itself: the fog baked into the surface shader is left out, or the fog would be applied twice.
# Argument: the converter checkout (default the IW8 Radiant worktree).

ROOT = (sys.argv[1] if len(sys.argv) > 1 else 'C:/zsw-iw8r/scriptables').rstrip('/') + '/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


D = 'src/convert/iw3_iw8/'
patch(D + 'replay_vision_names.inc', '''    {"fogSpline", "MLOSSOSRMM"},
''', '''    {"fogSpline", "MLOSSOSRMM"},
    // The veil and the volumetric air (fields 137-147; the names are IW7's, which Replay keeps at the same offsets).
    {"veilStrength", "MMQLTMKTPK"},
    {"volumetricAirDensity", "NSKNLMKQSR"},
    {"volumetricAbsorption", "MQMPKLKK"},
    {"volumetricAlbedo", "NKKRQROOQK"},
    {"volumetricSunBrightness", "LKNNNKTPTM"},
    {"volumetricSpotBrightness", "MQSQOLLMRM"},
    {"volumetricOmniBrightness", "RRKMLNPKP"},
    {"volumetricAmbientBrightness", "MQRSTPRORQ"},
    {"volumetricHeightFogBaseHeight", "NPKPLTKRNS"},
    {"volumetricHeightFogHalfPlaneDistance", "MLPOMNQPPK"},
    {"volumetricSunAnisotropy", "MTNSRNMTKN"},
''')
patch(D + 'iw3_vision.cpp', '''    if (!gradingClut.empty())
        overrides["NONMKTKNNM"] = gradingClut; // grading CLUT (stock: <map>_clut)
''', '''    if (!gradingClut.empty())
        overrides["NONMKTKNNM"] = gradingClut; // grading CLUT (stock: <map>_clut)
    // The atmospheric fog's sun glow points at the map's sun in both styles (stock visions name their own direction;
    // the table default is not the map's). Nothing reads it while the atmospheric fog is off.
    if (!overrides.contains("RLNQONPKS"))
        overrides["RLNQONPKS"] = Vector(sunDirection); // fogSunDir
''')
F = D + 'iw3_fastfile.cpp'
patch(F, '''    {
        // An IW7 extraction's own volumetric air, veil and light scatter (iw7_volumetrics).
        const auto volumetrics = world.find("iw7_volumetrics");''', '''    // True when the map's vision turns the game's atmospheric fog on (IW8 Radiant: vision overrides with
    // fogAtmosphericScattering 1). The game then fogs every opaque pixel and the sky itself, so the surface shader
    // bakes no fog (it would be applied twice).
    bool gameFog = false;
    {
        // An IW7 extraction's own volumetric air, veil and light scatter (iw7_volumetrics).
        const auto volumetrics = world.find("iw7_volumetrics");''')
patch(F, '''                if (visionOverrides.is_discarded() || !visionOverrides.is_object())
                    throw std::runtime_error("invalid vision overrides: " + path.string());
            }
        }
''', '''                if (visionOverrides.is_discarded() || !visionOverrides.is_object())
                    throw std::runtime_error("invalid vision overrides: " + path.string());
                for (const char *key : {"fogAtmosphericScattering", "MKKLQTSKSO"})
                    if (const auto value = visionOverrides.find(key); value != visionOverrides.end())
                        gameFog = value->is_number()    ? value->get<float>() >= 0.5f
                                  : value->is_boolean() ? value->get<bool>()
                                  : value->is_string()  ? std::atof(value->get<std::string>().c_str()) >= 0.5
                                                        : false;
                if (gameFog)
                {
                    zt::info("iw3: the map's vision turns the game's atmospheric fog on: no fog is baked into the surface shader");
                    if (expFog || nativeFog)
                        zt::warn("iw3: the map also has a script or vision fog of its own; it is left out, the game's fog replaces it");
                }
            }
        }
''')
patch(F, '''                            FogShaderDefines(expFog, options.style, ReadMapSun(exportRoot, sourceMap),
                                             lightingMatch.lightUnitScale, nativeFog),''',
      '''                            FogShaderDefines(gameFog ? std::optional<ExpFog>{} : expFog, options.style,
                                             ReadMapSun(exportRoot, sourceMap), lightingMatch.lightUnitScale,
                                             gameFog ? std::optional<NativeFog>{} : nativeFog),''')
print('ok')
