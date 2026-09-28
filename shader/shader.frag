#version 330 core

in vec2 UV;
out vec4 FragColor;

uniform int uShapeType; // 0 = Rectangle, 1 = Circle/Capsule

void main() {
    // Pure white base color
    vec3 baseColor = vec3(1.0, 1.0, 1.0);

    // Light direction vector coming from top-right
    vec2 lightDir = normalize(vec2(0.5, 0.8));

    float shadowFactor = 1.0;

    if (uShapeType == 1) { // Circle / Capsule
        float dist = length(UV);

        // Discard pixels outside the unit circle bounds
        if (dist > 1.0) {
            discard;
        }

        // Generate a 3D hemisphere normal from 2D coordinates
        vec3 normal = vec3(UV.x, UV.y, sqrt(max(0.0, 1.0 - dist * dist)));

        // Diffuse light directional shadow
        float diff = max(dot(normal.xy, lightDir), 0.0);
        shadowFactor = mix(0.7, 1.0, diff);

        // Edge ambient occlusion (soft vignette shadow along border)
        float edgeOcclusion = smoothstep(0.65, 1.0, dist);
        shadowFactor *= mix(1.0, 0.6, edgeOcclusion);

    } else { // Rectangle
        // Pseudo-normal generation for flat box edges
        vec2 absUV = abs(UV);
        float edgeDist = max(absUV.x, absUV.y);

        // Soft drop-off near edges
        float edgeShadow = smoothstep(0.7, 1.0, edgeDist);

        // Combine directional shading with edge occlusion
        float diff = max(dot(-UV, lightDir), 0.0);
        shadowFactor = mix(0.75, 1.0, diff) * mix(1.0, 0.65, edgeShadow);
    }

    // Multiply bright white color by calculated shadow gradient
    FragColor = vec4(baseColor * shadowFactor, 1.0);
}