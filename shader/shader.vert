#version 330 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;

out vec2 UV;

uniform vec2 uPos;
uniform float uAngle;
uniform vec2 uSize;
uniform float uAspectRatio;

void main() {
    UV = aUV;

    // Apply local scaling (body size)
    vec2 scaled = aPos * (uSize * 0.1);

    // Apply rotation
    float c = cos(uAngle);
    float s = sin(uAngle);
    vec2 rotated = vec2(
            scaled.x * c - scaled.y * s,
            scaled.x * s + scaled.y * c
    );

    // Screen-space position with aspect ratio correction
    vec2 screenPos = vec2(
            uPos.x * 0.1 * uAspectRatio + rotated.x * uAspectRatio,
            (uPos.y * 0.1 - 0.6) + rotated.y
    );

    gl_Position = vec4(screenPos, 0.0, 1.0);
}