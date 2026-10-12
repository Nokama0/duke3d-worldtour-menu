// Overlay pixel shader. The atlas contributes nothing but coverage, in alpha;
// the colour is the vertex's. One path then serves the glyph quads, the solid
// rectangles (which sample a fully opaque texel), and both atlases: the
// readout's R8G8B8A8 cells and the panel's A8_UNORM one, whose rgb reads as
// zero and must not reach the target. (2026-09-11)
Texture2D    gTex : register(t0);
SamplerState gSmp : register(s0);

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };

float4 main(VSOut i) : SV_Target {
  return float4(i.col.rgb, i.col.a * gTex.Sample(gSmp, i.uv).a);
}
