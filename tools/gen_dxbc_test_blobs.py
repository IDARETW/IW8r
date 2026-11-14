"""Compiles the small pixel shaders tests/dxbc_splice_tests.cpp embeds for the splicer's screen mode and prints
them as C++ byte arrays (d3dcompiler_47.dll, ps_5_0, no debug info). The project's own shaders; nothing of the game.

Usage: py -3 -I tools/gen_dxbc_test_blobs.py > out.inc
"""
import ctypes
import sys

SHADERS = {
    # A host with the screen position (all four parts) and a 2D texture at t3.
    'kScreenHost': '''
Texture2D<float4> mask : register(t3);
Texture2D<float4> colourMap : register(t0);
SamplerState samp : register(s0);
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 c = colourMap.Sample(samp, uv);
    c.rgb *= 1.0 - mask.Load(int3(int2(position.xy), 0)).r * frac(position.w + position.z);
    return min(c, 32255.0);
}''',
    # A host without any screen position input.
    'kPlainHost': '''
Texture2D<float4> colourMap : register(t0);
SamplerState samp : register(s0);
float4 main(float2 uv : TEXCOORD0) : SV_Target { return min(colourMap.Sample(samp, uv), 32255.0); }''',
    # A screen filter: colour in input 0, SV_Position in input 1, the scene depth at t0 (Load), a screen derivative.
    'kScreenFilter': '''
Texture2D<float4> sceneZ : register(t0);
float4 main(float4 colour : TEXCOORD0, float4 position : SV_Position) : SV_Target {
    const float depth = abs(sceneZ.Load(int3(int2(position.xy), 0)).x) * 6.0;
    const float slope = abs(ddx(1.0 / position.w)) + abs(ddy(1.0 / position.w));
    const float ink = saturate((depth - position.w) / max(position.w, 1.0) - slope);
    return float4(colour.rgb * (1.0 - ink), colour.a);
}''',
}


def compile_ps(source):
    dll = ctypes.WinDLL('d3dcompiler_47.dll')
    fn = dll.D3DCompile
    fn.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p,
                   ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p)]
    fn.restype = ctypes.c_long
    data = source.encode('ascii')
    blob, errors = ctypes.c_void_p(), ctypes.c_void_p()
    hr = fn(data, len(data), b'test', None, None, b'main', b'ps_5_0', 0, 0, ctypes.byref(blob), ctypes.byref(errors))

    def text(b):
        vt = ctypes.cast(ctypes.cast(b, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
        p = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt[3])(b)
        n = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt[4])(b)
        return ctypes.string_at(p, n)
    if hr < 0:
        raise SystemExit('compile failed: ' + (text(errors).decode('latin-1') if errors else hex(hr & 0xffffffff)))
    return text(blob)


def main():
    for name, source in SHADERS.items():
        code = compile_ps(source)
        print('const std::vector<uint8_t> %s = {' % name)
        for i in range(0, len(code), 24):
            print('    ' + ' '.join('0x%02x,' % b for b in code[i:i + 24]))
        print('};')
        sys.stderr.write('%s: %d bytes\n' % (name, len(code)))


if __name__ == '__main__':
    main()
