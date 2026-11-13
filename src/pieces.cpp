#include "pieces.hpp"

#include <raymath.h>

#include <cmath>

#include "chess.hpp"

namespace {

struct ProfilePoint { float r, y; };
using Profile = std::vector<ProfilePoint>;

// Surface of revolution around the y axis. The profile runs bottom to top and should start
// and end at r = 0 so the solid is closed. Normals are smooth around, faceted along the profile.
Mesh lathe(const Profile& prof, int slices = 40) {
    const int segs = static_cast<int>(prof.size()) - 1;
    Mesh m{};
    m.vertexCount = segs * slices * 6;
    m.triangleCount = segs * slices * 2;
    m.vertices = static_cast<float*>(MemAlloc(m.vertexCount * 3 * sizeof(float)));
    m.normals = static_cast<float*>(MemAlloc(m.vertexCount * 3 * sizeof(float)));
    m.texcoords = static_cast<float*>(MemAlloc(m.vertexCount * 2 * sizeof(float)));

    int v = 0;
    auto emit = [&](ProfilePoint p, float th, float nr, float ny) {
        const float c = cosf(th), s = sinf(th);
        m.vertices[v * 3 + 0] = p.r * c;
        m.vertices[v * 3 + 1] = p.y;
        m.vertices[v * 3 + 2] = p.r * s;
        m.normals[v * 3 + 0] = nr * c;
        m.normals[v * 3 + 1] = ny;
        m.normals[v * 3 + 2] = nr * s;
        m.texcoords[v * 2 + 0] = th / (2 * PI);
        m.texcoords[v * 2 + 1] = p.y;
        ++v;
    };

    for (int i = 0; i < segs; ++i) {
        const ProfilePoint a = prof[i], b = prof[i + 1];
        const float dr = b.r - a.r, dy = b.y - a.y;
        const float len = hypotf(dr, dy);
        const float nr = len > 1e-6f ? dy / len : 0.0f;
        const float ny = len > 1e-6f ? -dr / len : 1.0f;
        for (int j = 0; j < slices; ++j) {
            const float t0 = 2 * PI * j / slices, t1 = 2 * PI * (j + 1) / slices;
            emit(a, t0, nr, ny); emit(b, t0, nr, ny); emit(b, t1, nr, ny);
            emit(a, t0, nr, ny); emit(b, t1, nr, ny); emit(a, t1, nr, ny);
        }
    }
    UploadMesh(&m, false);
    return m;
}

// Append a spherical arc (centre on the axis at height cy) from polar angle phi0 to phi1.
void arc(Profile& p, float cy, float radius, float phi0, float phi1, int steps = 10) {
    for (int k = 0; k <= steps; ++k) {
        const float phi = phi0 + (phi1 - phi0) * k / steps;
        p.push_back({fmaxf(0.0f, radius * sinf(phi)), cy - radius * cosf(phi)});
    }
}

Profile base() {
    return {{0, 0}, {0.34f, 0}, {0.34f, 0.06f}, {0.31f, 0.09f}, {0.31f, 0.12f}, {0.25f, 0.15f}};
}

Profile operator+(Profile a, const Profile& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Matrix boxAt(Vector3 size, float tiltX, Vector3 at) {
    return MatrixMultiply(MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixRotateX(tiltX)),
                          MatrixTranslate(at.x, at.y, at.z));
}

}  // namespace

void PieceSet::load() {
    using namespace chess;
    auto own = [&](Mesh m) { owned_.push_back(m); return m; };
    const Matrix id = MatrixIdentity();

    cube_ = own(GenMeshCube(1, 1, 1));
    disc_ = own(lathe({{0, 0}, {1, 0}, {1, 1}, {0, 1}}, 32));

    Profile pawn = base() + Profile{{0.15f, 0.30f}, {0.12f, 0.40f}, {0.19f, 0.43f}, {0.19f, 0.46f}, {0.10f, 0.48f}};
    arc(pawn, 0.58f, 0.13f, 0.85f, PI);
    parts_[PAWN] = {{own(lathe(pawn)), id}};

    Profile rook = base() + Profile{{0.21f, 0.20f}, {0.18f, 0.52f}, {0.25f, 0.56f}, {0.25f, 0.74f},
                                    {0.18f, 0.74f}, {0.18f, 0.67f}, {0, 0.67f}};
    parts_[ROOK] = {{own(lathe(rook)), id}};
    // Crenellations: four blocks on the rim.
    for (int i = 0; i < 4; ++i) {
        const float a = PI / 4 + i * PI / 2;
        parts_[ROOK].push_back({cube_, MatrixMultiply(MatrixMultiply(MatrixScale(0.1f, 0.08f, 0.1f), MatrixRotateY(-a)),
                                                      MatrixTranslate(0.2f * cosf(a), 0.78f, 0.2f * sinf(a)))});
    }

    Profile bishop = base() + Profile{{0.18f, 0.22f}, {0.10f, 0.52f}, {0.18f, 0.55f}, {0.18f, 0.58f}, {0.09f, 0.60f}};
    arc(bishop, 0.72f, 0.14f, 0.7f, PI - 0.35f);
    arc(bishop, 0.90f, 0.045f, 0.4f, PI, 6);
    parts_[BISHOP] = {{own(lathe(bishop)), id}};

    Profile queen = base() + Profile{{0.20f, 0.22f}, {0.11f, 0.62f}, {0.21f, 0.66f}, {0.21f, 0.69f},
                                     {0.12f, 0.72f}, {0.21f, 0.88f}, {0.18f, 0.90f}, {0.12f, 0.88f}};
    arc(queen, 0.94f, 0.07f, 1.0f, PI, 8);
    parts_[QUEEN] = {{own(lathe(queen)), id}};

    Profile king = base() + Profile{{0.21f, 0.22f}, {0.12f, 0.68f}, {0.23f, 0.72f}, {0.23f, 0.75f},
                                    {0.13f, 0.78f}, {0.21f, 0.95f}, {0.15f, 0.98f}, {0, 0.98f}};
    parts_[KING] = {{own(lathe(king)), id},
                    {cube_, boxAt({0.07f, 0.24f, 0.07f}, 0, {0, 1.10f, 0})},
                    {cube_, boxAt({0.20f, 0.07f, 0.07f}, 0, {0, 1.12f, 0})}};

    // Knight: turned base plus a blocky horse head facing -z.
    Profile knightBase = base() + Profile{{0.24f, 0.20f}, {0.22f, 0.26f}, {0, 0.26f}};
    parts_[KNIGHT] = {{own(lathe(knightBase)), id},
                      {cube_, boxAt({0.28f, 0.48f, 0.34f}, -0.25f, {0, 0.48f, 0.04f})},   // neck
                      {cube_, boxAt({0.24f, 0.20f, 0.46f}, -0.45f, {0, 0.70f, -0.10f})},  // head
                      {cube_, boxAt({0.08f, 0.48f, 0.10f}, -0.25f, {0, 0.60f, 0.20f})},   // mane
                      {cube_, boxAt({0.20f, 0.10f, 0.06f}, 0, {0, 0.86f, 0.08f})}};       // ears
}

void PieceSet::unload() {
    for (Mesh& m : owned_) UnloadMesh(m);
    owned_.clear();
    for (auto& p : parts_) p.clear();
}
