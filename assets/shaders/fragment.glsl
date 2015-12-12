#version 410

in vec2 TexCoord;
in vec4 Position;

uniform sampler2D Texture;

out vec4 FragColor;

void main() {
    FragColor = texture(Texture, TexCoord);
    if (length(Position) > 3) {
        FragColor *= 0;
    } else if (length(Position) > 2) {
        FragColor *= 0.5;
    }
}
