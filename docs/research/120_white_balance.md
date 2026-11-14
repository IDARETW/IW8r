# 120. The game's white balance

**Why.** PLAN_REMAINING 1.8 left "white balance is not decoded" open (research 109, side finding 5: Hideout's vision
sets one, so the tone map pass's 3x3 matrix is not the identity there). Most of the stock maps set one. Without it the
viewport's Hideout was cold and grey where the game is warm.

**Labels.** VERIFIED = read in the Replay 1.20 exe (constant tables) or in the stock vision files. INFERRED = read in
the IW8 game-test decompile ("GT", `gfx_d3d\r_whitebalance.cpp`), which is not Replay; the Replay function at
`0x140EF8500` uses the same constants and calls, but its arithmetic was not compared line by line. Nothing was run in
the game.

## 1. Where it is applied (research 95 / 109, VERIFIED there)

`cs_tonemap`: veil mix, vignette, then `x = (CB0[0..2] 3x3) * c`, then the exposure and the 3D LUT. `CB0[0..2]` is the
matrix of `R_WhiteBalance_SetMatrix(viewInfo.whiteBalance, viewInfo.tonemap.whitePoint)`.

## 2. The vision fields (VERIFIED on the stock vision files)

| Token | Field | Values |
|---|---|---|
| `MNMNSSLLOR` | whiteBalanceMethod | 0 sRGB scale, 1 CAT02, 2 Sharp, 3 Bradford, 4 Von Kries, 5 XYZ scaling, 6 none |
| `NLSRLSTPTN` | whiteBalanceIlluminant | 0 the white point, 1 D custom, 2 black body custom, 3 .. 21 the CIE table below |
| `TRQLLNNLS` | whiteBalanceColorTempK | kelvin (dvar range 2,000 .. 40,000; D custom valid 4,000 .. 25,000) |
| `OPSMSQNKQ` | whiteBalanceGreenMagentaShift | tint |
| `LRROOOSOMM` | toneWhitePoint | linear sRGB, used by illuminant 0 |

The names of the first four come from `cg_visionsets_common.cpp:448-451` (GT) and the order of the tokens next to
`toneWhitePoint` in the vision files; the method names from the exe's strings (`CAT02`, `Sharp`, `Bradford`,
`Von Kries`, `XYZ Scaling` in that order, `D Custom`, `Blackbody Custom`).

| Map | method | illuminant | K | tint | White becomes (R, G, B) |
|---|---|---|---|---|---|
| mp_hideout | 4 | 1 | 5500 | 2.2 | 1.078, 0.988, 0.891 |
| mp_donetsk, mp_rust, mp_br_quarry | 4 | 1 | 5500 | 0 | 1.069, 0.991, 0.881 |
| mp_backlot2 | 4 | 1 | 5700 | -1.2 | 1.088, 0.990, 0.838 |
| mp_m_fork | 4 | 1 | 5400 | 0 | 1.056, 0.993, 0.903 |
| mp_village2 | 4 | 1 | 5000 | 0 | 1.000, 1.000, 1.002 |
| mp_aniyah_tac, mp_emporium, mp_m_speed | 0 | 2 | 5000 | 0 | 0.976, 1.009, 0.994 |
| mp_m_king | 0 | 0 | | | identity |
| mp_m_speedball, mp_shipment | not set | | | | identity |

5,000 K is neutral; a higher temperature makes the picture warmer.

## 3. The matrix (INFERRED arithmetic, VERIFIED constants)

Vectors are rows; `a . M` applies `M`.

1. **The illuminant's chromaticity (x, y).**
   - 0: with method 0 the result is `diag(L / w)` for the white point `w` (`L` its Rec. 709 luminance); with method 1
     a CAT02 form on `w`; with methods 2 .. 5 the xy of `w . sRGBD65ToXYZ`.
   - 1: the CIE D formula: `x = 2967800/T^2 - 4607000100/T^3 + 99.11/T + 0.244063` up to 7,000 K, else
     `1901800/T^2 - 2006400000/T^3 + 247.48/T + 0.23704`; `y = (2.87 - 3x) x - 0.275`. Then the tint.
   - 2: the game's black body fit: `x = 0.243185 - 1.8738099e15/T^5 + 5.8426602e12/T^4 - 6615550000/T^3 +
     2888860/T^2 + 164.35201/T`, `y = 2.8780601 x - 0.291251 - x (0.70877975 - 5.4613505e15/T^5 + 1.7028843e13/T^4 -
     1.9281484e10/T^3 + 8419785/T^2 + 479.01544/T)`. Then the tint.
   - 3 .. 21: the table's 2 degree x, y, then the tint along the black body fit at the table's kelvin.
2. **The tint.** In CIE 1960 uv (`u = 2x / (1.5 - x + 6y)`, `v = 3y / (1.5 - x + 6y)`): the unit direction from the
   point to the locus point one kelvin further, turned by -90 degrees, times `tint / 3000`. Back to xy
   (`x = 1.5u / (u - 4v + 2)`, `y = v / (u - 4v + 2)`), clamped inside the triangle.
3. **The adaptation.** Method 6: identity. Else, with `C = XYZToCRD[method]`, `S = (x/y, 1, (1-x-y)/y)` and
   `D65 = (0.95047, 1, 1.0888)`:
   `matrix = sRGBD50ToXYZ . C . diag((D65 . C) / (S . C)) . C^-1 . XYZTosRGBD65`.
   The first factor is the D50 form of sRGB, so a D50 illuminant gives the identity (exactly with Bradford).

**Tables** (Replay 1.20 exe; rows as stored):

- `XYZToCRD` at `.rdata 0x1423E6950`, 36 bytes a method: 0 = XYZ to sRGB; 1 CAT02 `0.7328 -0.7036 0.003 / 0.4296
  1.6975 0.0136 / -0.1624 0.0061 0.9834`; 2 Sharp `1.2694 -0.8364 0.0297 / -0.0988 1.8006 -0.0315 / -0.1706 0.0357
  1.0018`; 3 Bradford `0.8951 -0.7502 0.0389 / 0.2664 1.7135 -0.0685 / -0.1614 0.0367 1.0296`; 4 Von Kries `0.40024
  -0.2263 0 / 0.7076 1.16532 0 / -0.08081 0.0457 0.91822`; 5 and 6 identity.
- `sRGBD50ToXYZ` at `0x1423E6A50`: `0.4360747 0.2225045 0.0139322 / 0.3850649 0.7168786 0.0971045 / 0.1430804
  0.0606169 0.7141733`. `sRGBD65ToXYZ` at `0x1423E6A78`, `XYZTosRGBD65` at `0x1423E6AA0`, `sRGBToCAT02LMS` at
  `0x1423E6AC8`: the standard values (core `white_balance.cpp` lists them).
- Illuminants at `.data 0x14411C190`, 20 bytes each (x, y at 2 degrees, x, y at 10 degrees, kelvin): 3 A 2856, 4 B
  4874, 5 C 6774, 6 D50 5003, 7 D55 5503, 8 D65 6504, 9 D75 7504, 10 .. 21 F1 .. F12.

The numbers in section 2 were calculated twice: by core `gameWhiteBalanceMatrix` and by a separate script in column
form (`XYZTosRGB . C^-1 . diag . C . sRGBToXYZ_D50`); they agree to four decimals, and a test holds Hideout's values.

## 4. What the editor does (2026-11-14)

- Core `white_balance.{h,cpp}`: `gameWhiteBalanceFromVision`, `gameWhiteBalanceFromOverrides`,
  `gameWhiteBalanceMatrix`; `GamePostFx::whiteBalance`.
- View > Game Lighting: the post pass (`ps_post`) multiplies the linear colour by the matrix after the vignette and
  before the exposure, and keeps it at 0 or above. A stock map takes its vision's values; an own map the four tokens
  (and `toneWhitePoint`) in worldspawn `iw8r_vision`, which the build writes into the map's vision.
- View > Game Lighting Options > White Balance (saved as `game/whiteBalance`, on by default).
- A clean capture (a screenshot without the editor's markers) now goes through the same post pass as the view; it
  skipped the whole pass before, so it had no bloom and no vignette, and would have had no white balance.
- Hideout, bench camera: the picture's mean colour goes from (92, 100, 97) to (100, 98, 91); the frame time is the
  same (8.0 ms).

## 5. Not determined

- Whether Replay's arithmetic equals the game-test build's in every step (same constants and calls; not compared
  instruction by instruction). `FastSinCos(-pi/2)` is taken as exactly (-1, 0).
- Whether the game clamps negative results (a strongly saturated colour can leave the range); the editor clamps at 0.
- The exposure meter: the editor meters before the matrix. The matrix keeps the luminance of white within 1 % on
  the maps above.
- An own map: the four fields reach the build as `iw8r_vision` overrides by token, like any other override; a built
  zone's vision was not read back for them.
- Not compared with the running game.
