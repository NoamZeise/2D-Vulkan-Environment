#ifndef GL_MODEL_LOADER_H
#define GL_MODEL_LOADER_H

#include "texture_loader.h"
#include <resource_loader/vertex_model.h>
#include <graphics/model_loader.h>
#include <graphics/logger.h>

#include <map>

class AssimpLoader;

struct GPUMesh {
    Resource::Texture texture;
    glm::vec4 diffuseColour;

    template <typename T_Vert>
    void load(Mesh<T_Vert>* data) {
	diffuseColour = data->diffuseColour;
	texture = data->texture;
    }
};

struct GPUModel {
    std::vector<Resource::ModelAnimation> animations;
    std::map<std::string, int> animationMap;
    Resource::ModelType type;

    template <typename T_Vert>
    GPUModel(LoadedModel<T_Vert> &model) {
	animations.resize(model.animations.size());
	for (int i = 0; i < model.animations.size(); i++) {
	    animations[i] = model.animations[i];
	    animationMap[model.animations[i].getName()] = i;
	}   
    }

    Resource::ModelAnimation getAnimation(int index) {
	if (index >= animations.size()) {
	    LOG_ERROR("Model animation index was out of range. "
		      "animation index: " << index
		      << " - size: " << animations.size());
	    return Resource::ModelAnimation();
	}
	return animations[index];
    }

    Resource::ModelAnimation getAnimation(std::string animation) {
	if (animationMap.find(animation) == animationMap.end()) {
	    LOG_ERROR("No animation called " << animation << " could be found in the"
		  " animation map for model");
	return Resource::ModelAnimation();
    }        
	return getAnimation(animationMap[animation]);  
    }
};

class InternalModelLoader : public ModelLoader {
public:
    InternalModelLoader(Resource::Pool pool, InternalTexLoader* texLoader);
    ~InternalModelLoader();
    
    Resource::Model load(
	    Resource::ModelType type,
	    std::string path,
	    std::vector<Resource::ModelAnimation>* pAnimations) override;
    Resource::Model load(
	    Resource::ModelType type,
	    ModelInfo::Model &modelData,
	    std::vector<Resource::ModelAnimation>* pAnimations) override;

    virtual void loadGPU() = 0;
    virtual void clearGPU() {};
    void clearStaged();
    
protected:
    Resource::Pool pool;
    InternalTexLoader *texLoader;
    unsigned int currentIndex = 0;
    ModelGroup<Vertex2D> stage2D;
    ModelGroup<Vertex3D> stage3D;
    ModelGroup<VertexAnim3D> stageAnim3D;
    Resource::Model quad;
    AssimpLoader* loader;
    template <class T_Vert>
    Resource::Model loadData(ModelInfo::Model& model,
			     ModelGroup<T_Vert>& modelGroup,
			     std::vector<Resource::ModelAnimation> *pAnimations);
    void loadQuad();
};



#endif
