import io

# core dxbc_splice: a filter may also read the screen (stock map ink lines from the scene depth). In screen mode the
# filter program has a second input, SV_Position, and one texture, t0 (the scene depth, Load only): the position is
# mapped to the stock shader's own position input and the texture to the register the technique binds the scene
# depth at.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/core/dxbc_splice.h'
patch(H, '''// The container's checksum is recomputed (dxbcChecksum: Direct3D refuses a blob whose checksum is wrong).
#pragma once''', '''// A filter that reads the screen (DxbcSpliceOptions::sceneDepthRegister >= 0; a map shader's iw8rFilterScreen, the ink
// lines of a stock map): its program also has input 1, SV_Position, and the texture t0, read with Load only. Input 1
// becomes the stock shader's own position input (it must have one that carries every part the filter reads), t0
// becomes the stock shader's texture register `sceneDepthRegister`, where the technique binds the scene depth; when
// the stock shader does not declare that register the filter's declaration is added.
//
// The container's checksum is recomputed (dxbcChecksum: Direct3D refuses a blob whose checksum is wrong).
#pragma once''')
patch(H, '''// The stock shader with the filter shader's instructions between its last output write and its final ret (see the
// top). False with *err when a requirement is not met; `out` is then empty.
bool dxbcSpliceFilter(const std::vector<uint8_t>& stock, const std::vector<uint8_t>& filter, std::vector<uint8_t>& out,
                      std::string* err = nullptr);''', '''struct DxbcSpliceOptions {
    // >= 0: the filter reads the screen (see the top); the stock shader's texture register for the scene depth.
    int sceneDepthRegister = -1;
};

// The stock shader with the filter shader's instructions between its last output write and its final ret (see the
// top). False with *err when a requirement is not met; `out` is then empty.
bool dxbcSpliceFilter(const std::vector<uint8_t>& stock, const std::vector<uint8_t>& filter, std::vector<uint8_t>& out,
                      std::string* err = nullptr, const DxbcSpliceOptions& options = {});''')

C = 'src/core/dxbc_splice.cpp'
patch(C, '''                   kOpDclInputPs = 98, kOpDclOutput = 101;
constexpr uint32_t kTypeTemp = 0, kTypeInput = 1, kTypeOutput = 2, kTypeImm32 = 4, kTypeImm64 = 5, kTypeNull = 13;''',
      '''                   kOpDclInputPs = 98, kOpDclOutput = 101, kOpDclResource = 88, kOpDclInputPsSiv = 100;
constexpr uint32_t kTypeTemp = 0, kTypeInput = 1, kTypeOutput = 2, kTypeImm32 = 4, kTypeImm64 = 5, kTypeNull = 13, kTypeResource = 7;
constexpr uint32_t kNamePosition = 1;       // the system value name of SV_Position
constexpr uint32_t kDimensionTexture2D = 3; // dcl_resource's dimension field (bits 11..15)

// The components (bit 0 x .. bit 3 w) an operand token names: its mask, its swizzle or its one selected component.
uint32_t operandComponents(uint32_t tok) {
    if ((tok & 3u) != 2u) return 0xFu;  // not a four-component operand: taken as all
    const uint32_t mode = (tok >> 2) & 3u;
    if (mode == 0) return (tok >> 4) & 0xFu;
    if (mode == 1) return (1u << ((tok >> 4) & 3u)) | (1u << ((tok >> 6) & 3u)) | (1u << ((tok >> 8) & 3u)) | (1u << ((tok >> 10) & 3u));
    return 1u << ((tok >> 4) & 3u);
}''')
patch(C, '''bool dxbcSpliceFilter(const std::vector<uint8_t>& stock, const std::vector<uint8_t>& filter, std::vector<uint8_t>& out, std::string* err) {
    out.clear();''', '''bool dxbcSpliceFilter(const std::vector<uint8_t>& stock, const std::vector<uint8_t>& filter, std::vector<uint8_t>& out, std::string* err,
                      const DxbcSpliceOptions& options) {
    out.clear();
    const bool screen = options.sceneDepthRegister >= 0;''')
patch(C, '''    bool haveTemps = false, endsInRet = false, sawCode = false;
    std::vector<std::pair<size_t, size_t>> outputOperands;  // operand token, index token''', '''    bool haveTemps = false, endsInRet = false, sawCode = false;
    int positionInput = -1;          // the stock shader's SV_Position input register
    uint32_t positionMask = 0;       // the components it declares
    int sceneDepthDimension = -1;    // the dimension the stock shader declares for the scene depth register (-1: none)
    std::vector<std::pair<size_t, size_t>> outputOperands;  // operand token, index token''')
patch(C, '''        if (isDeclaration(in.op)) {
            if (!sawCode) declEnd = at + in.tokens;
            continue;
        }
        sawCode = true;
        endsInRet = in.op == kOpRet;''', '''        if (isDeclaration(in.op)) {
            if (!sawCode) declEnd = at + in.tokens;
            if (screen && (in.op == kOpDclInputPsSiv || in.op == kOpDclResource) && in.tokens >= 3) {
                Operand d;
                if (parseOperand(s.code, in.operandsAt, at + in.tokens, d) && d.dims == 1 && d.immediateIndex) {
                    if (in.op == kOpDclInputPsSiv && s.code[at + in.tokens - 1] == kNamePosition)
                        positionInput = int(s.code[d.index0]), positionMask = operandComponents(s.code[d.at]);
                    if (in.op == kOpDclResource && d.type == kTypeResource && int(s.code[d.index0]) == options.sceneDepthRegister)
                        sceneDepthDimension = int((s.code[at] >> 11) & 31u);
                }
            }
            continue;
        }
        sawCode = true;
        endsInRet = in.op == kOpRet;''')
patch(C, '''    if (!writes) return fail("stock shader: it never writes output 0");
''', '''    if (!writes) return fail("stock shader: it never writes output 0");
    if (screen && positionInput < 0) return fail("stock shader: it has no screen position input for a filter that reads the screen");
    if (screen && sceneDepthDimension >= 0 && uint32_t(sceneDepthDimension) != kDimensionTexture2D)
        return fail("stock shader: its register t" + std::to_string(options.sceneDepthRegister) + " is not a 2D texture");
''')
patch(C, '''    bool filterEnded = false;
    for (size_t at = 0; at < f.code.size(); at += in.tokens) {''', '''    bool filterEnded = false;
    std::vector<uint32_t> textureDeclaration;  // the filter's dcl of t0, with the stock register (screen mode)
    for (size_t at = 0; at < f.code.size(); at += in.tokens) {''')
patch(C, '''            } else if (in.op == kOpDclInputPs || in.op == kOpDclOutput) {
                Operand o;
                if (!parseOperand(f.code, in.operandsAt, at + in.tokens, o) || !o.immediateIndex || f.code[o.index0] != 0)
                    return fail("filter shader: it may only read input 0 and write output 0");
            } else if (in.op != kOpDclGlobalFlags) {''', '''            } else if (in.op == kOpDclInputPs || in.op == kOpDclOutput) {
                Operand o;
                if (!parseOperand(f.code, in.operandsAt, at + in.tokens, o) || !o.immediateIndex || f.code[o.index0] != 0)
                    return fail("filter shader: it may only read input 0 and write output 0");
            } else if (screen && in.op == kOpDclInputPsSiv) {
                Operand o;
                if (!parseOperand(f.code, in.operandsAt, at + in.tokens, o) || !o.immediateIndex || f.code[o.index0] != 1 ||
                    f.code[at + in.tokens - 1] != kNamePosition)
                    return fail("filter shader: its second input must be the screen position");
            } else if (screen && in.op == kOpDclResource) {
                Operand o;
                if (!parseOperand(f.code, in.operandsAt, at + in.tokens, o) || o.type != kTypeResource || o.dims != 1 || !o.immediateIndex ||
                    f.code[o.index0] != 0 || ((f.code[at] >> 11) & 31u) != kDimensionTexture2D)
                    return fail("filter shader: its only texture must be the 2D texture t0 (the scene depth)");
                textureDeclaration.assign(f.code.begin() + long(at), f.code.begin() + long(at + in.tokens));
                textureDeclaration[o.index0 - at] = uint32_t(options.sceneDepthRegister);
            } else if (in.op != kOpDclGlobalFlags) {''')
patch(C, '''            } else if (o.type == kTypeInput) {
                if (copy[idx] != 0) return fail("filter shader: it reads an input other than 0");
                copy[rel] = (copy[rel] & ~(0xFFu << 12)) | (kTypeTemp << 12);
                copy[idx] = result;
            } else if (o.type == kTypeOutput) {''', '''            } else if (o.type == kTypeInput && screen && copy[idx] == 1) {
                // The screen position: the stock shader's own, which must carry what the filter reads.
                if (operandComponents(copy[rel]) & ~positionMask)
                    return fail("stock shader: its screen position input lacks a part the filter reads");
                copy[idx] = uint32_t(positionInput);
            } else if (o.type == kTypeInput) {
                if (copy[idx] != 0) return fail("filter shader: it reads an input other than 0");
                copy[rel] = (copy[rel] & ~(0xFFu << 12)) | (kTypeTemp << 12);
                copy[idx] = result;
            } else if (o.type == kTypeResource && screen) {
                if (copy[idx] != 0) return fail("filter shader: it reads a texture other than t0");
                copy[idx] = uint32_t(options.sceneDepthRegister);
            } else if (o.type == kTypeOutput) {''')
patch(C, '''    s.code.insert(s.code.begin() + long(lastRet), body.begin(), body.end());
    if (!haveTemps) {
        const uint32_t dcl[2] = {kOpDclTemps | (2u << 24), temps};
        s.code.insert(s.code.begin() + long(declEnd), dcl, dcl + 2);
    }''', '''    s.code.insert(s.code.begin() + long(lastRet), body.begin(), body.end());
    if (!haveTemps) {
        const uint32_t dcl[2] = {kOpDclTemps | (2u << 24), temps};
        s.code.insert(s.code.begin() + long(declEnd), dcl, dcl + 2);
    }
    // The scene depth's declaration, when the stock shader has none for that register (after the insert above, which
    // is at or behind it, so declEnd still holds).
    if (screen && sceneDepthDimension < 0 && !textureDeclaration.empty())
        s.code.insert(s.code.begin() + long(declEnd), textureDeclaration.begin(), textureDeclaration.end());''')
print('ok')
