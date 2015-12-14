#version 330

layout(location = 0) in vec3 VertexPosition;
layout(location = 1) in vec2 VertexTexCoord;

uniform mat4 MVP;
uniform mat4 ModelMat;
uniform mat4 ViewMat;

out vec2 TexCoord;
out vec4 Position;

void main() {
    TexCoord = VertexTexCoord;
    Position = ViewMat * ModelMat * vec4(VertexPosition, 1.0);
    gl_Position = MVP * vec4(VertexPosition, 1.0);
}
