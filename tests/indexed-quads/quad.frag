#version 450
layout(location = 0) flat in vec4 color;
layout(location = 0) out vec4 target;
void main() {
    if (color.a == 0) discard;
    target = color;
}
