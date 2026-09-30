#version 410 core


in vec3 vNormal;

uniform vec3 uColor;
uniform vec3 uLightDirection;

out vec4 FragColor;

void main(){
    vec3 normal = normalize(vNormal);

    float diffuse = max(dot(normal, -uLightDirection), 0.0);
    float ambient = 0.25;
    float brightness = ambient + diffuse * 0.75;

    FragColor = vec4(uColor * brightness, 1.0);
}