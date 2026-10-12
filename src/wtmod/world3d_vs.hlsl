// The in-world layer's vertex shader: a pass-through.
//
// The CPU has already multiplied each corner by the engine's own world-to-clip
// matrix (R+0x178) and applied the [-1,1] to [0,1] depth remap that its
// gluPerspective projection needs before Direct3D will accept it, so there is
// nothing left to transform here.
struct VSIn  { float4 pos : POSITION; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR0; };
VSOut main(VSIn i) {
  VSOut o;
  o.pos = i.pos;
  o.col = i.col;
  return o;
}
