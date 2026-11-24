#version 450
layout(location=0) flat in vec4 first;
layout(location=0) out vec4 target;
void main() {
    target = vec4(first.rgb, gl_FrontFacing ? 1.0 : 0.5);
}
