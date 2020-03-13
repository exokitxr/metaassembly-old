#pragma once

#include <memory>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

enum class ETextureType
{
	Invalid = 0,
	D3D11Texture2D = 1,
};

enum class ETextureFormat
{
	R8G8B8A8 = 1,
	B8G8R8A8 = 2,
};


class IVrManager;

class ITransform
{
public:
	virtual ~ITransform() {}


	virtual void setNullTransform( uint64_t parentId) = 0;
	virtual void setOriginTransform( const std::string & originPath ) = 0;

	virtual void setParentFromThisMatrix( const glm::mat4 & parentFromTransform ) = 0;
	virtual void setParentFromThisTRS( const glm::vec3 & translation, const glm::vec3 & scale, const glm::quat & rot ) = 0;

	virtual void setTransitionTime( float time ) = 0;
};


class IModelInstance
{
public:
	virtual ~IModelInstance() {}

	virtual void setUniverseFromModel( const glm::mat4 & universeFromModel ) = 0;
	virtual void setOverrideTexture( void *textureHandle, ETextureType type, ETextureFormat format,
		uint32_t width, uint32_t height ) = 0;
	virtual void setBaseColor( const glm::vec4 & color ) = 0;
};


#ifndef _WINDEF_
class HINSTANCE__; // Forward or never
typedef HINSTANCE__* HINSTANCE;
#endif

struct AABB_t
{
	float xMin = 0, xMax = 0;
	float yMin = 0, yMax = 0;
	float zMin = 0, zMax = 0;
};

class IRenderer
{
public:
	virtual ~IRenderer() {}

	virtual void init( HINSTANCE hInstance, IVrManager *vrManager ) = 0;
	virtual void runFrame( bool *shouldQuit, double frameTime ) = 0;

	virtual std::unique_ptr<IModelInstance> loadModelInstance(const std::string &uri, unsigned char *data, size_t size) = 0;
  virtual std::unique_ptr<IModelInstance> createDefaultModelInstance(const std::string &modelUrl) = 0;
  virtual void setModelTransform(IModelInstance *model, float *position, size_t numPositions, float *quaternion, size_t numQuaternions, float *scale, size_t numScales) = 0;
  virtual void setModelMatrix(IModelInstance *model, float *matrix, size_t numMatrix) = 0;
  virtual std::unique_ptr<IModelInstance> setModelGeometry(std::unique_ptr<IModelInstance> model, float *positions, size_t numPositions, float *normals, size_t numNormals, float *colors, size_t numColors, float *uvs, size_t numUvs, uint16_t *indices, size_t numIndices) = 0;
  virtual std::unique_ptr<IModelInstance> setModelTexture(std::unique_ptr<IModelInstance> modelInstance, int width, int height, unsigned char *data, size_t size) = 0;
  virtual void setBoneTexture(IModelInstance *modelInstance, float *boneTexture, size_t numBoneTexture) = 0;
	virtual void resetRenderList() = 0;
	virtual void addToRenderList( IModelInstance *modelInstance ) = 0;
	virtual void removeFromRenderList( IModelInstance *modelInstance ) = 0;
	virtual void update() = 0;
	virtual void processRenderList() = 0;
	virtual bool getModelBox( const std::string & uri, AABB_t *pBox, std::string *psError = nullptr ) = 0;
};

