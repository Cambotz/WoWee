#version 450

layout(push_constant) uniform Push {
    vec2 gridOffset;   // position of the quad's origin, in grid cells
    float gridCols;
    float gridRows;
    vec2 gridScale;    // size of the quad, in grid cells (1,1 = a whole cell)
    vec2 uvScale;      // how much of the texture the quad actually covers
} push;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

layout(location = 0) out vec2 TexCoord;

void main() {
    // An overlay piece is neither a whole cell nor a whole texture. Its last
    // row and column are whatever is left of the overlay, and the file holding
    // one is padded out to the next power of two, so the quad carries both its
    // real size and the fraction of the file it should sample.
    TexCoord = aUV * push.uvScale;
    vec2 pos = (aPos * push.gridScale + push.gridOffset) / vec2(push.gridCols, push.gridRows);
    pos = pos * 2.0 - 1.0;
    gl_Position = vec4(pos, 0.0, 1.0);
}
