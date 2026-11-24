#version 410 core


in vec3 vNormal;
in vec4 vLightSpacePosition;

uniform vec3 uColor;
uniform vec3 uLightDirection;
uniform sampler2D uShadowMap;

out vec4 FragColor;

float calculateShadow(vec4 lightSpacePosition, vec3 normal){
    vec3 projected = lightSpacePosition.xyz / lightSpacePosition.w;

    projected = projected * 0.5 + 0.5;

    if (projected.z > 1.0){
        return 0.0;
    }

    float currentDepth = projected.z;

    float lightFacing = max(dot(normalize(normal), -normalize(uLightDirection)), 0.0);
    float bias = max(0.005 * (1.0 - lightFacing), 0.0005);

    vec2 texelSize = 1.0 / textureSize(uShadowMap, 0);
    float shadow = 0.0;

    for (int x = -1; x <= 1; ++x){
        for (int y = -1; y <= 1; ++y){
            float closestDepth =
            texture(uShadowMap, projected.xy + vec2(x, y) * texelSize).r;

            shadow += currentDepth - bias > closestDepth? 1.0: 0.0;
        }
    }

    shadow /= 9.0;

    return shadow;
}
void main(){
    vec3 normal = normalize(vNormal);
    float diffuse = max(dot(normal, -uLightDirection), 0.0);

    float ambient = 0.25;
    float shadow = calculateShadow(vLightSpacePosition, vNormal);
    float brightness = ambient + (1.0 - shadow) * diffuse * 0.75;

    FragColor = vec4(uColor * brightness, 1.0);
}