#version 450
layout(push_constant) uniform Test { uint overlap; } test;
layout(location = 0) flat out vec4 color;
const vec2 corners[] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,1));
void main() {
    int corner = gl_VertexIndex % 4;
    gl_Position = vec4(corners[corner], 0, 1);
    if (test.overlap == 0) {
        color = corner == 0 ? vec4(1,0,0,1) : vec4(0,0,0,1);
    } else {
        int quad = gl_VertexIndex / 4;
        bool visible = quad != (gl_InstanceIndex - 3);
        color = vec4(float(quad), float(gl_InstanceIndex - 3), 0, float(visible));
    }
}
