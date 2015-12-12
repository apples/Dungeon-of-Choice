#version 410

layout(location = 0) in vec3 VertexPosition;
layout(location = 1) in vec2 VertexTexCoord;

uniform mat4 MVP;

out vec2 TexCoord;
out vec4 Position;

void main() {
    TexCoord = VertexTexCoord;
    Position = MVP * vec4(VertexPosition, 1.0);
    gl_Position = Position;
}
