#version 330

in vec2 TexCoord;
in vec4 Position;

uniform sampler2D Texture;
uniform float BrightRadius;
uniform float DimRadius;
uniform bool FullBright;
uniform bool EnableFisheye;
uniform float FisheyeTheta;

out vec3 OutColor;

vec2 fisheye(vec2 src)
{
    if (EnableFisheye) {
        float z = sqrt(1.0 - src.x * src.x - src.y * src.y);
        float a = 1.0 / (z * tan(FisheyeTheta * 0.5));
        return ((src) * a);
    }
    return src;
}

void main() {
    vec4 FragColor = texture(Texture, fisheye(TexCoord - 0.5) + 0.5);
    if (FragColor.a < 0.5) {
        discard;
    }
    if (!FullBright) {
        if (length(Position) > DimRadius) {
            FragColor *= 0;
        } else if (length(Position) > BrightRadius) {
            FragColor *= 0.33;
        } else {
            FragColor *= 0.66;
        }
    }
    OutColor = FragColor.rgb;
}
