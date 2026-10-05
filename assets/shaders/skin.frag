#version 410 core
in vec3 vNormal;
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    vec3 normal = normalize(vNormal);
    float key = max(dot(normal, normalize(vec3(0.6, 1.0, 0.8))), 0.0);
    float fill = max(dot(normal, normalize(vec3(-1.0, 0.3, -0.4))), 0.0);
    // High ambient light keeps the mannequin white while revealing its curved surfaces.
    float brightness = 0.83 + 0.13 * key + 0.03 * fill;
    FragColor = vec4(uColor * brightness, 1.0);
}
