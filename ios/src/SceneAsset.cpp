#include <iostream>
#include <chrono>

#include <filament/Engine.h>
#include <filament/TransformManager.h>
#include <filament/Texture.h>

#include <gltfio/Animator.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>

#include <imageio/ImageDecoder.h>

#include "StreamBufferAdapter.hpp"
#include "SceneAsset.hpp"
#include "Log.hpp"
#include "SceneResources.hpp"

using namespace std::chrono;

namespace polyvox {

using namespace std;
using namespace filament;
using namespace filament::gltfio;
using namespace image;
using namespace utils;

SceneAsset::SceneAsset(FilamentAsset *asset, Engine *engine,
                       NameComponentManager *ncm, LoadResource loadResource, FreeResource freeResource)
    : _asset(asset), _engine(engine), _ncm(ncm), _loadResource(loadResource), _freeResource(freeResource) {
  _animator = _asset->getAnimator();
  for (int i = 0; i < _animator->getAnimationCount(); i++) {
    _embeddedAnimationStatus.push_back(
        EmbeddedAnimationStatus(i, _animator->getAnimationDuration(i), false));
  }
  Log("Created animation buffers for %d", _embeddedAnimationStatus.size());
}

SceneAsset::~SceneAsset() { 
  // most other destructor work is deferred to SceneAssetLoader so we don't need to do anything here
  if(_texture) {
    _engine->destroy(_texture);
    _texture = nullptr;
  }
}

void SceneAsset::applyWeights(float *weights, int count) {
  RenderableManager &rm = _engine->getRenderableManager();
  for (size_t i = 0, c = _asset->getEntityCount(); i != c; ++i) {
    auto inst = rm.getInstance(_asset->getEntities()[i]);
    rm.setMorphWeights(inst, weights, count);
  }
}

void SceneAsset::animateWeights(float *data, int numWeights, int numFrames,
                                float frameLengthInMs) {
  Log("Making morph animation buffer with %d weights across %d frames and "
      "frame length %f ms ",
      numWeights, numFrames, frameLengthInMs);
  _morphAnimationBuffer = std::make_unique<MorphAnimationStatus>(
      data, numWeights, numFrames, frameLengthInMs);
}

void SceneAsset::updateAnimations() {
  updateMorphAnimation();
  updateEmbeddedAnimations();
}

void SceneAsset::updateMorphAnimation() {
  if (!_morphAnimationBuffer) {
    return;
  }

  if (_morphAnimationBuffer->frameIndex == -1) {
    _morphAnimationBuffer->frameIndex++;
    _morphAnimationBuffer->startTime = high_resolution_clock::now();
    applyWeights(_morphAnimationBuffer->frameData,
                 _morphAnimationBuffer->numWeights);
  } else {
    duration<double, std::milli> dur =
        high_resolution_clock::now() - _morphAnimationBuffer->startTime;
    int frameIndex =
        static_cast<int>(dur.count() / _morphAnimationBuffer->frameLengthInMs);

    if (frameIndex > _morphAnimationBuffer->numFrames - 1) {
      duration<double, std::milli> dur =
          high_resolution_clock::now() - _morphAnimationBuffer->startTime;
      Log("Morph animation completed in %f ms (%d frames at framerate %f), "
          "final frame was %d",
          dur.count(), _morphAnimationBuffer->numFrames,
          1000 / _morphAnimationBuffer->frameLengthInMs,
          _morphAnimationBuffer->frameIndex);
      _morphAnimationBuffer = nullptr;
    } else if (frameIndex != _morphAnimationBuffer->frameIndex) {
      Log("Rendering frame %d (of a total %d)", frameIndex,
          _morphAnimationBuffer->numFrames);
      _morphAnimationBuffer->frameIndex = frameIndex;
      auto framePtrOffset = frameIndex * _morphAnimationBuffer->numWeights;
      applyWeights(_morphAnimationBuffer->frameData + framePtrOffset,
                   _morphAnimationBuffer->numWeights);
    }
  }
}

void SceneAsset::playAnimation(int index, bool loop) {
  Log("Playing animation at index %d", index);
  if (index > _animator->getAnimationCount() - 1) {
    Log("Asset does not contain an animation at index %d", index);
  } else if (_embeddedAnimationStatus[index].started) {
    Log("Animation already playing, call stop first.");
  } else {
    Log("Starting animation at index %d", index);
    _embeddedAnimationStatus[index].play = true;
    _embeddedAnimationStatus[index].loop = loop;
  }
}

void SceneAsset::stopAnimation(int index) {
  // TODO - does this need to be threadsafe?
  _embeddedAnimationStatus[index].play = false;
  _embeddedAnimationStatus[index].started = false;
}

void SceneAsset::loadTexture(const char* resourcePath, int renderableIndex) {

  Log("Loading texture at %s for renderableIndex %d", resourcePath, renderableIndex);

  string rp(resourcePath);

  if(_texture) {
    _engine->destroy(_texture);
    _texture = nullptr;
  }
  
  ResourceBuffer imageResource = _loadResource(rp.c_str());
  
  StreamBufferAdapter sb((char *)imageResource.data, (char *)imageResource.data + imageResource.size);

  istream *inputStream = new std::istream(&sb);

  LinearImage *image = new LinearImage(ImageDecoder::decode(
      *inputStream, rp.c_str(), ImageDecoder::ColorSpace::SRGB));

  if (!image->isValid()) {
    Log("Invalid image : %s", rp.c_str());
    return;
  }

  uint32_t channels = image->getChannels();
  uint32_t w = image->getWidth();
  uint32_t h = image->getHeight();
  _texture = Texture::Builder()
                      .width(w)
                      .height(h)
                      .levels(0xff)
                      .format(channels == 3 ? Texture::InternalFormat::RGB16F
                                            : Texture::InternalFormat::RGBA16F)
                      .sampler(Texture::Sampler::SAMPLER_2D)
                      .build(*_engine);

  Texture::PixelBufferDescriptor::Callback freeCallback = [](void *buf, size_t,
                                                            void *data) {
    delete reinterpret_cast<LinearImage *>(data);
  };

  Texture::PixelBufferDescriptor buffer(
      image->getPixelRef(), size_t(w * h * channels * sizeof(float)),
      channels == 3 ? Texture::Format::RGB : Texture::Format::RGBA,
      Texture::Type::FLOAT, freeCallback);

  _texture->setImage(*_engine, 0, std::move(buffer));
  setTexture();
  delete inputStream;

  _freeResource(imageResource.id);
  
}

void SceneAsset::setTexture() {
  
  MaterialInstance* const* inst = _asset->getMaterialInstances();
  size_t mic =  _asset->getMaterialInstanceCount();
  Log("Material instance count : %d", mic);
    
  RenderableManager &rm = _engine->getRenderableManager();
  auto sampler = TextureSampler();
  inst[0]->setParameter("baseColorIndex",0);
  inst[0]->setParameter("baseColorMap",_texture,sampler);

}

void SceneAsset::updateEmbeddedAnimations() {
  auto now = high_resolution_clock::now();
  for (auto &status : _embeddedAnimationStatus) {
    if (!status.play) {
      // Log("Skipping animation %d", status.animationIndex);
      continue;
    }
    duration<double> dur =
        duration_cast<duration<double>>(now - status.startedAt);
    float animationTimeOffset = 0;
    bool finished =  false;
    if (!status.started) {
      status.started = true;
      status.startedAt = now;
    } else if (dur.count() >= status.duration) {
      if (status.loop) {
        status.startedAt = now;
      } else {
        animationTimeOffset = dur.count();
        finished = true;
      }
    } else {
      animationTimeOffset = dur.count();
    }

    if (!finished) {
      _animator->applyAnimation(status.animationIndex, animationTimeOffset);
    } else {
      Log("Animation %d finished", status.animationIndex);

      status.play = false;
      status.started = false;
    }
  }

  _animator->updateBoneMatrices();
}

unique_ptr<vector<string>> SceneAsset::getAnimationNames() {
  size_t count = _animator->getAnimationCount();

  Log("Found %d animations in asset.", count);

  unique_ptr<vector<string>> names = make_unique<vector<string>>();

  for (size_t i = 0; i < count; i++) {
    names->push_back(_animator->getAnimationName(i));
  }

  return names;
}

unique_ptr<vector<string>> SceneAsset::getTargetNames(const char *meshName) {
  if (!_asset) {
    Log("No asset, ignoring call.");
    return nullptr;
  }
  Log("Retrieving morph target names for mesh  %s", meshName);
  unique_ptr<vector<string>> names = make_unique<vector<string>>();
  const Entity *entities = _asset->getEntities();
  RenderableManager &rm = _engine->getRenderableManager();
  for (int i = 0; i < _asset->getEntityCount(); i++) {
    Entity e = entities[i];
    auto inst = _ncm->getInstance(e);
    const char *name = _ncm->getName(inst);
    Log("Got entity instance name %s", name);
    if (strcmp(name, meshName) == 0) {
      size_t count = _asset->getMorphTargetCountAt(e);
      for (int j = 0; j < count; j++) {
        const char *morphName = _asset->getMorphTargetNameAt(e, j);
        names->push_back(morphName);
      }
      break;
    }
  }
  return names;
}

void SceneAsset::transformToUnitCube() {
  if (!_asset) {
    Log("No asset, cannot transform.");
    return;
  }
  Log("Transforming asset to unit cube.");
  auto &tm = _engine->getTransformManager();
  auto aabb = _asset->getBoundingBox();
  auto center = aabb.center();
  auto halfExtent = aabb.extent();
  auto maxExtent = max(halfExtent) * 2;
  auto scaleFactor = 2.0f / maxExtent;
  auto transform =
      math::mat4f::scaling(scaleFactor) * math::mat4f::translation(-center);
  tm.setTransform(tm.getInstance(_asset->getRoot()), transform);
}

void SceneAsset::updateTransform() {
  auto &tm = _engine->getTransformManager();
  auto transform = 
      _position * _rotation * math::mat4f::scaling(_scale);
  tm.setTransform(tm.getInstance(_asset->getRoot()), transform);
}

void SceneAsset::setScale(float scale) {
  _scale = scale;
  updateTransform();
}

void SceneAsset::setPosition(float x, float y, float z) {
  Log("Setting position to %f %f %f", x, y, z);
  _position = math::mat4f::translation(math::float3(x,y,z));
  updateTransform();
}

void SceneAsset::setRotation(float rads, float x, float y, float z) {
  Log("Rotating %f radians around axis %f %f %f", rads, x, y, z);
  _rotation = math::mat4f::rotation(rads, math::float3(x,y,z));
  updateTransform();
}


const utils::Entity *SceneAsset::getCameraEntities() {
  return _asset->getCameraEntities();
}

size_t SceneAsset::getCameraEntityCount() {
  return _asset->getCameraEntityCount();
}

const Entity* SceneAsset::getLightEntities() const noexcept { 
  return _asset->getLightEntities();
}

size_t SceneAsset::getLightEntityCount() const noexcept {
  return _asset->getLightEntityCount();
}


} // namespace polyvox
