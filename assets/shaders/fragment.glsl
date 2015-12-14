#version 410

in vec2 TexCoord;
in vec4 Position;

uniform sampler2D Texture;
uniform float BrightRadius;
uniform float DimRadius;
uniform bool FullBright;

out vec4 FragColor;

void main() {
    FragColor = texture(Texture, TexCoord);
    if (FragColor.a < 0.5) {
        discard;
    }
    if (FullBright) {
        return;
    }
    if (length(Position) > DimRadius) {
        FragColor *= 0;
    } else if (length(Position) > BrightRadius) {
        FragColor *= 0.33;
    } else {
        FragColor *= 0.66;
    }
}
