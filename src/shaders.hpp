#pragma once

// Simple two-light Blinn-Phong shading. Desktop uses GLSL 330, the web build (WebGL 1) GLSL 100.
// Attribute and uniform names follow raylib's defaults so DrawMesh fills them in.

#ifdef __EMSCRIPTEN__
#define CHESS_GLSL_VS_HEADER "#version 100\nattribute vec3 vertexPosition;\nattribute vec3 vertexNormal;\nvarying vec3 fragPos;\nvarying vec3 fragNormal;\n"
#define CHESS_GLSL_FS_HEADER "#version 100\nprecision mediump float;\nvarying vec3 fragPos;\nvarying vec3 fragNormal;\n"
#define CHESS_GLSL_FS_OUT "gl_FragColor"
#define CHESS_GLSL_FS_DECL ""
#else
#define CHESS_GLSL_VS_HEADER "#version 330\nin vec3 vertexPosition;\nin vec3 vertexNormal;\nout vec3 fragPos;\nout vec3 fragNormal;\n"
#define CHESS_GLSL_FS_HEADER "#version 330\nin vec3 fragPos;\nin vec3 fragNormal;\n"
#define CHESS_GLSL_FS_OUT "finalColor"
#define CHESS_GLSL_FS_DECL "out vec4 finalColor;\n"
#endif

inline const char* kLitVS = CHESS_GLSL_VS_HEADER R"(
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
void main() {
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

inline const char* kLitFS = CHESS_GLSL_FS_HEADER CHESS_GLSL_FS_DECL R"(
uniform vec4 colDiffuse;
uniform vec3 viewPos;
void main() {
    vec3 n = normalize(fragNormal);
    vec3 v = normalize(viewPos - fragPos);
    vec3 key = normalize(vec3(-0.5, 1.0, 0.6));
    vec3 fill = normalize(vec3(0.6, 0.5, -0.7));
    float diffuse = max(dot(n, key), 0.0) * 0.75 + max(dot(n, fill), 0.0) * 0.25;
    float spec = pow(max(dot(n, normalize(key + v)), 0.0), 32.0) * 0.3;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.15;
    vec3 c = colDiffuse.rgb * (0.30 + diffuse) + vec3(spec + rim);
    )" CHESS_GLSL_FS_OUT R"( = vec4(c, colDiffuse.a);
}
)";
