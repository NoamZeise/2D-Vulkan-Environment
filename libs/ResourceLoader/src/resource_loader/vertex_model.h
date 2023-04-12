#ifndef VERTEX_MODEL_H
#define VERTEX_MODEL_H

#include <resources/resources.h>
#include <vector>
#include <string>
#include <stdint.h>
#include <resources/model/model_info.h>
#include <vertex_types.h>

template <class T_Vert>
struct Mesh {
    Mesh() {}
    std::vector<T_Vert> verticies;
    std::vector<unsigned int> indicies;
    Resource::Texture texture;
    glm::vec4 diffuseColour;

    std::string texToLoad = "";
    void processMeshInfo(ModelInfo::Mesh &dataMesh);
};

template <class T_Vert>
struct LoadedModel {
    LoadedModel(){}
    int ID = -1;
    std::vector<Mesh<T_Vert>*> meshes;
    std::string        directory;
    std::vector<Resource::ModelAnimation> animations;
};

template <class T_Vert>
struct ModelGroup {
    std::vector<LoadedModel<T_Vert>> models;
    size_t vertexDataOffset;
    size_t vertexDataSize;
    void loadModel(ModelInfo::Model &modelData, uint32_t currentID);
};

//loadVerticies defined for Vertex Types

void loadVertices(Mesh<VertexAnim3D> *mesh, ModelInfo::Mesh &dataMesh);
void loadVertices(Mesh<Vertex3D> *mesh, ModelInfo::Mesh &dataMesh);
void loadVertices(Mesh<Vertex2D> *mesh, ModelInfo::Mesh &dataMesh);


template <class T_Vert>
void ModelGroup<T_Vert>::loadModel(ModelInfo::Model &modelData,
				   uint32_t currentID) {
    this->models.push_back(LoadedModel<T_Vert>());
    auto model = &this->models[this->models.size() - 1];
    model->ID = currentID;
    for(auto& meshData: modelData.meshes) {
	model->meshes.push_back(new Mesh<T_Vert>());
	Mesh<T_Vert>* mesh = model->meshes.back();
	mesh->processMeshInfo(meshData);
    }
}

template <class T_Vert>
void Mesh<T_Vert>::processMeshInfo(ModelInfo::Mesh &dataMesh) {
    if(dataMesh.diffuseTextures.size() > 0)
	this->texToLoad = dataMesh.diffuseTextures[0];
    this->diffuseColour = dataMesh.diffuseColour;
    loadVertices(this, dataMesh);
    this->indicies = dataMesh.indicies;
}

#endif
