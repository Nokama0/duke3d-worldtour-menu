// The in-world layer's pixel shader: the vertex colour, untouched. Blending
// and the depth test are pipeline state, not shader work.
struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR0; };
float4 main(PSIn i) : SV_TARGET { return i.col; }
