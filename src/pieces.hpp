#pragma once
#include <raylib.h>

#include <vector>

// One drawable piece of a chess-man: a mesh and its transform relative to the piece origin
// (piece origin = centre of the square at board height, facing -z).
struct PiecePart {
    Mesh mesh;
    Matrix local;
};

// Procedurally generated chess-men (lathe profiles + boxes), so no model assets are needed.
class PieceSet {
public:
    void load();
    void unload();

    const std::vector<PiecePart>& parts(int type) const { return parts_[type]; }
    Mesh cube() const { return cube_; }  // unit cube centred on the origin
    Mesh disc() const { return disc_; }  // radius 1, height 1 cylinder with its base at y = 0

private:
    std::vector<PiecePart> parts_[7];
    std::vector<Mesh> owned_;
    Mesh cube_{}, disc_{};
};
