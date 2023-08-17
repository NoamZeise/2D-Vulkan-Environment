#ifndef TEXTURE_LOADER_H
#define TEXTURE_LOADER_H

#include <vector>
#include <string>

#include "../render_structs/device_state.h"
#include <graphics/render_config.h>
#include <graphics/resources.h>

namespace Resource {

  //for internal texture storage
  struct TextureInMemory;
  struct TextureInGPU;

  class TextureLoader {
  public:
      TextureLoader(DeviceState base, VkCommandPool pool,
		    ResourcePool resPool, RenderConfig config);
      ~TextureLoader();
      void UnloadGPU();
      void UnloadStaged(); 
      Texture LoadTexture(std::string path);
      // takes ownership of data
      Texture LoadTexture(unsigned char* data, int width, int height, int nrChannels);
      void endLoading();
      float getMinMipmapLevel();
      uint32_t getImageCount();
      VkImageView getImageViewSetIndex(uint32_t texID, uint32_t imageViewIndex);
      uint32_t getViewIndex(uint32_t texID);
      
  private:
      
      VkDeviceSize stageTexDataCreateImages(VkBuffer &stagingBuffer,
					    VkDeviceMemory &stagingMemory,
					    uint32_t *pFinalMemType);
      void textureDataStagingToFinal(VkBuffer stagingBuffer,
				     VkCommandBuffer &cmdbuff);
      
      bool srgb;
      bool mipmapping;
      bool useNearestTextureFilter;
      
      DeviceState base;
      ResourcePool resPool;
      VkCommandPool pool;
      
      std::vector<TextureInMemory> texToLoad;
      std::vector<TextureInGPU> textures;
      VkDeviceMemory memory;
      uint32_t minimumMipmapLevel;
  };
}
#endif
