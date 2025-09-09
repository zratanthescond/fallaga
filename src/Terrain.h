#pragma once
#include <vector>
#include "ObjectModel.h"
struct Tree {
    float x, y, z;
};

struct Rock {
    float x, y, z, size;
};

class ObjModel;

class Terrain {
public:
    Terrain();
    ~Terrain();
    void render() const;
    ObjModel* getModel() {return terrainModel; };
    float getHeight(float x, float z) const;
    
private:
    std::vector<Tree> trees;
    std::vector<Rock> rocks;

    void drawTree(float x, float y, float z) const;
    void drawRock(float x, float y, float z, float size) const;
    ObjModel* treeModel;
    ObjModel* rockModel;
    ObjModel* terrainModel;
    
    unsigned int treeDisplayList;
    unsigned int rockDisplayList;
    unsigned int terrainDisplayList;
    void createDisplayLists();
    
};
