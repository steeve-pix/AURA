#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
const float uOutlineWidth = 0.016;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vec3 normal = normalize(transpose(inverse(mat3(uModel))) * aNormal);
    world.xyz += normal * uOutlineWidth;
    gl_Position = uProjection * uView * world;
}
