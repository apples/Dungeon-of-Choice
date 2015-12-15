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

float xfov_to_yfov(float xfov, float aspect) {
    return 2.0f * atan(tan(xfov * 0.5f) / aspect);
}

vec2 fisheye(vec2 src)
{
    if (EnableFisheye) {
        float z = sqrt(1.0 - src.x * src.x - src.y * src.y);
        float t = tan(FisheyeTheta);
        float a = 1.0 / (z * t);
        vec2 c = 2.0 * vec2(0.5,0.5) / (sqrt(0.5) * t); // refit to corners
        return (src * a / c);
    }
    return src;
}

vec2 fisheye2(vec2 src)
{
    if (EnableFisheye) {
        float b = (3.14159 - FisheyeTheta) / 2.0;
        float y = tan(b);
        float a = atan(y, src.x);
        src.x = 2.0 * a / FisheyeTheta;
    }
    return src;
}

vec2 fisheye3(vec2 src, vec2 fovs)
{
    if (EnableFisheye) {
        vec2 b = (3.14159 - fovs) / 2.0;
        vec2 y = tan(b);
        vec2 a = src * fovs / 2.0;
        src = y * tan(a);
    }
    return src;
}

void main() {
    vec4 FragColor = texture(Texture, fisheye(TexCoord - 0.5) + 0.5);
    //vec4 FragColor = texture(Texture, fisheye3(TexCoord * 2.0 - 1.0, vec2(FisheyeTheta, xfov_to_yfov(FisheyeTheta, 16.0/9.0))) / 2.0 + 0.5);
    if (FragColor.a < 0.5) {
        discard;
    }
    if (!FullBright) {
        if (length(Position) > DimRadius) {
            FragColor *= 0;
        } else if (length(Position) > BrightRadius) {
            FragColor *= 0.30;
        } else {
            FragColor *= 0.80;
        }
    }
    OutColor = FragColor.rgb;
}
