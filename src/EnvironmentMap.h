#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <glm/glm.hpp>

namespace env {
struct WaterSource { glm::vec2 center{0.0f}; float radius = 1.0f; };
struct RiverSegment { glm::vec2 p0{0.0f}; glm::vec2 p1{0.0f}; float radius = 1.0f; };
struct BuildParams {
  float minX=0, maxX=1, minZ=0, maxZ=1;
  int resolution=1024;
  float waterLevel=0;
  std::function<float(float,float)> heightAt;
  // Optional authoritative coverage from the water renderer's simulation map.
  // When provided, it replaces the height/disc approximation so shore bands,
  // terrain texturing and vegetation use the same accepted water footprint.
  std::function<float(float,float)> waterCoverageAt;
  // Water surface elevation at covered texels. The nearest covered texel's
  // level is used to form beaches around inland lakes and streams as well as sea.
  std::function<float(float,float)> waterSurfaceAt;
  std::vector<WaterSource> extraWater;
  std::vector<RiverSegment> riverSegments;
  std::function<float(float,float)> beachHintAt;
  float wetBand=3, beachWidth=28, beachMaxHeight=3, riparianStart=6, riparianEnd=60;
};
class EnvironmentMap {
public:
  bool build(const BuildParams&);
  // Stores vegetation-mask density in the packed texture's alpha channel.
  // Packed channels are R=shore, G=wetness, B=riparian, A=plant cover.
  // Call after the vegetation mask is built and whenever that mask changes.
  void updateCover(const std::function<float(float,float)>& coverAt);
  bool ready() const { return n_>0; }
  int resolution() const { return n_; }
  uint32_t revision() const { return revision_; }
  float signedWaterDistance(float x,float z) const;
  float waterDistance(float x,float z) const;
  bool inWater(float x,float z) const;
  float shore(float x,float z) const;
  float wetness(float x,float z) const;
  float riparian(float x,float z) const;
  float cover(float x,float z) const;
  float vegetationFactor(float x,float z) const;
  const std::vector<uint8_t>& packedRGBA() const { return rgba_; }
  glm::vec4 worldRect() const { return {minX_,minZ_,maxX_-minX_,maxZ_-minZ_}; }
private:
  float sampleDistance(float x,float z) const;
  float sampleChannel(int c,float x,float z) const;
  int n_=0; uint32_t revision_=0;
  float minX_=0,maxX_=1,minZ_=0,maxZ_=1;
  std::vector<float> signedDist_;
  std::vector<uint8_t> rgba_;
};
}
