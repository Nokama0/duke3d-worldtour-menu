// Overlay vertex shader. Pixels from the top left to clip space, using the
// back buffer size the caller puts in the constant buffer, so there is no
// projection matrix to get wrong. (2026-09-11)
cbuffer OverlayCB : register(b0) {
  float4 gViewport;        // xy = back buffer width and height in pixels
};

struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };

VSOut main(VSIn i) {
  VSOut o;
  o.pos = float4(i.pos.x / gViewport.x * 2.0f - 1.0f,
                 1.0f - i.pos.y / gViewport.y * 2.0f,
                 0.0f, 1.0f);
  o.uv  = i.uv;
  o.col = i.col;
  return o;
}
