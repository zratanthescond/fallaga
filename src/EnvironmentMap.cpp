#include "EnvironmentMap.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace env {
namespace {
float smooth(float a,float b,float x) {
  if (b<=a) return x>=b?1.0f:0.0f;
  float t=std::clamp((x-a)/(b-a),0.0f,1.0f); return t*t*(3-2*t);
}
uint8_t to8(float v) { return static_cast<uint8_t>(std::clamp(v,0.0f,1.0f)*255+0.5f); }
}
bool EnvironmentMap::build(const BuildParams& p) {
  if (!p.heightAt || p.resolution<16 || p.maxX<=p.minX || p.maxZ<=p.minZ || p.wetBand<0 || p.beachWidth<=0) return false;
  const int n=p.resolution; const size_t count=size_t(n)*n;
  minX_=p.minX; maxX_=p.maxX; minZ_=p.minZ; maxZ_=p.maxZ;
  // Store one sample at each GL_LINEAR texel centre. This makes CPU queries
  // and terrain/ocean shader lookups address the same world positions.
  const float dx=(maxX_-minX_)/n, dz=(maxZ_-minZ_)/n, cell=(dx+dz)*0.5f;
  std::vector<float> heights(count),d(count,std::numeric_limits<float>::max()*0.25f);
  std::vector<float> waterSurfaces(count,p.waterLevel);
  std::vector<int> nearestWater(count,-1);
  std::vector<uint8_t> water(count,0);
  for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
    size_t k=size_t(j)*n+i; float x=minX_+(i+0.5f)*dx,z=minZ_+(j+0.5f)*dz;
    heights[k]=p.heightAt(x,z);
    if (p.waterCoverageAt) {
      const float coverage=std::clamp(p.waterCoverageAt(x,z),0.0f,1.0f);
      // Match ocean.frag's connected-coverage cutoff so the shared mask does
      // not leave a vegetation/shoreline seam at weak fractional edge texels.
      water[k]=coverage>0.001f;
      if(water[k]) {
        waterSurfaces[k]=p.waterSurfaceAt ? p.waterSurfaceAt(x,z) : p.waterLevel;
        if(!std::isfinite(waterSurfaces[k])) waterSurfaces[k]=p.waterLevel;
        // The renderer first tests connected coverage, then discards fragments
        // above the sampled water surface. Apply that same height test here so
        // the shared sand/vegetation mask never expands pools onto dry banks.
        if (waterSurfaces[k] - heights[k] < -0.15f) {
          water[k]=0;
        }
      }
    } else {
      water[k]=heights[k]<=p.waterLevel-0.02f;
    }
  }
  // Extra source geometry is a startup fallback. Once the simulation supplies
  // coverage, rejected/sub-cell pools must not create phantom shorelines.
  if (!p.waterCoverageAt) for(const auto& s:p.extraWater) if(s.radius>0) {
    int i0=std::max(0,int(std::floor((s.center.x-s.radius-minX_)/dx))), i1=std::min(n-1,int(std::ceil((s.center.x+s.radius-minX_)/dx)));
    int j0=std::max(0,int(std::floor((s.center.y-s.radius-minZ_)/dz))), j1=std::min(n-1,int(std::ceil((s.center.y+s.radius-minZ_)/dz)));
    for(int j=j0;j<=j1;++j) for(int i=i0;i<=i1;++i) { float x=minX_+(i+0.5f)*dx-s.center.x,z=minZ_+(j+0.5f)*dz-s.center.y; if(x*x+z*z<=s.radius*s.radius) water[size_t(j)*n+i]=1; }
  }
  if (!p.waterCoverageAt) for(const auto& seg:p.riverSegments) if(seg.radius>0) {
    float minSegX = std::min(seg.p0.x, seg.p1.x) - seg.radius;
    float maxSegX = std::max(seg.p0.x, seg.p1.x) + seg.radius;
    float minSegZ = std::min(seg.p0.y, seg.p1.y) - seg.radius;
    float maxSegZ = std::max(seg.p0.y, seg.p1.y) + seg.radius;
    int i0=std::max(0,int(std::floor((minSegX-minX_)/dx))), i1=std::min(n-1,int(std::ceil((maxSegX-minX_)/dx)));
    int j0=std::max(0,int(std::floor((minSegZ-minZ_)/dz))), j1=std::min(n-1,int(std::ceil((maxSegZ-minZ_)/dz)));
    glm::vec2 ab = seg.p1 - seg.p0;
    float l2 = glm::dot(ab, ab);
    float r2 = seg.radius * seg.radius;
    for(int j=j0;j<=j1;++j) {
      float z = minZ_ + (j + 0.5f) * dz;
      for(int i=i0;i<=i1;++i) {
        float x = minX_ + (i + 0.5f) * dx;
        glm::vec2 ap = glm::vec2(x, z) - seg.p0;
        float t = l2 > 1e-6f ? std::clamp(glm::dot(ap, ab) / l2, 0.0f, 1.0f) : 0.0f;
        glm::vec2 closest = seg.p0 + t * ab;
        glm::vec2 diff = glm::vec2(x, z) - closest;
        if(glm::dot(diff, diff) <= r2) water[size_t(j)*n+i]=1;
      }
    }
  }
  // Land distance to water boundary (positive inland)
  for(size_t k=0;k<count;++k) if(water[k]) { d[k]=0; nearestWater[k]=static_cast<int>(k); }
  auto at=[&](int i,int j)->float& { return d[size_t(j)*n+i]; };
  constexpr float diag=1.41421356f;
  auto relaxLand=[&](int i,int j,int ni,int nj,float cost) {
    const size_t target=size_t(j)*n+i, source=size_t(nj)*n+ni;
    const float candidate=at(ni,nj)+cost;
    if(candidate<at(i,j)) { at(i,j)=candidate; nearestWater[target]=nearestWater[source]; }
  };
  for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
    if(i) relaxLand(i,j,i-1,j,1); if(j) relaxLand(i,j,i,j-1,1);
    if(i&&j) relaxLand(i,j,i-1,j-1,diag); if(i+1<n&&j) relaxLand(i,j,i+1,j-1,diag);
  }
  for(int j=n-1;j>=0;--j) for(int i=n-1;i>=0;--i) {
    if(i+1<n) relaxLand(i,j,i+1,j,1); if(j+1<n) relaxLand(i,j,i,j+1,1);
    if(i+1<n&&j+1<n) relaxLand(i,j,i+1,j+1,diag); if(i&&j+1<n) relaxLand(i,j,i-1,j+1,diag);
  }

  // Water distance to land boundary (negative in water for true geometric signed distance)
  std::vector<float> dWater(count, std::numeric_limits<float>::max()*0.25f);
  for(size_t k=0;k<count;++k) if(!water[k]) dWater[k]=0;
  auto atW=[&](int i,int j)->float& { return dWater[size_t(j)*n+i]; };
  for(int j=0;j<n;++j) for(int i=0;i<n;++i) { float v=atW(i,j); if(i) v=std::min(v,atW(i-1,j)+1); if(j) v=std::min(v,atW(i,j-1)+1); if(i&&j) v=std::min(v,atW(i-1,j-1)+diag); if(i+1<n&&j) v=std::min(v,atW(i+1,j-1)+diag); atW(i,j)=v; }
  for(int j=n-1;j>=0;--j) for(int i=n-1;i>=0;--i) { float v=atW(i,j); if(i+1<n) v=std::min(v,atW(i+1,j)+1); if(j+1<n) v=std::min(v,atW(i,j+1)+1); if(i+1<n&&j+1<n) v=std::min(v,atW(i+1,j+1)+diag); if(i&&j+1<n) v=std::min(v,atW(i-1,j+1)+diag); atW(i,j)=v; }

  signedDist_.resize(count); rgba_.assign(count*4,0);
  const float inf=std::numeric_limits<float>::max()*0.1f;
  for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
    size_t k=size_t(j)*n+i; float x=minX_+(i+0.5f)*dx,z=minZ_+(j+0.5f)*dz;
    if(water[k]) {
      // Distance transform values are centre-to-centre. Move half a texel
      // back to the rendered water edge for a correctly aligned signed field.
      float distWater = dWater[k]>=inf ? 1e5f : std::max(0.0f,dWater[k]*cell-0.5f*cell);
      signedDist_[k] = -distWater;
      rgba_[k*4+1]=255;
      // Alpha is reserved for plant cover and is filled after vegetation masks build.
      continue;
    }
    float dist=d[k]>=inf?1e5f:std::max(0.0f,d[k]*cell-0.5f*cell);
    float surface=p.waterLevel;
    if(p.waterSurfaceAt && nearestWater[k]>=0) surface=waterSurfaces[static_cast<size_t>(nearestWater[k])];
    float above=heights[k]-surface;
    float shore=(1-smooth(p.wetBand,p.beachWidth,dist))*(1-smooth(p.beachMaxHeight*.6f,p.beachMaxHeight,above));
    if(p.beachHintAt) {
      const float hint=std::clamp(p.beachHintAt(x,z),0.0f,1.0f);
      const float gate=(1-smooth(p.wetBand,p.beachWidth,dist)) *
                       (1-smooth(p.beachMaxHeight*0.6f,p.beachMaxHeight,above));
      shore=std::max(shore,hint*gate);
    }
    float wet=1-smooth(0,p.wetBand,dist);
    float rip=smooth(p.wetBand,p.riparianStart,dist)*(1-smooth(p.riparianEnd*.6f,p.riparianEnd,dist))*(1-shore);
    signedDist_[k]=dist; rgba_[k*4]=to8(shore); rgba_[k*4+1]=to8(wet); rgba_[k*4+2]=to8(rip);
  }
  n_=n; ++revision_; return true;
}
void EnvironmentMap::updateCover(const std::function<float(float,float)>& coverAt) {
  if (!ready()) return;
  const float dx=(maxX_-minX_)/n_, dz=(maxZ_-minZ_)/n_;
  for (int j=0;j<n_;++j) for (int i=0;i<n_;++i) {
    const size_t k=size_t(j)*n_+i;
    const float x=minX_+(i+0.5f)*dx, z=minZ_+(j+0.5f)*dz;
    const float v=(signedDist_[k]<=0.0f || !coverAt) ? 0.0f : coverAt(x,z);
    rgba_[k*4+3]=to8(v);
  }
  ++revision_;
}
float EnvironmentMap::sampleDistance(float x,float z) const {
  if(!ready()) return 1e5f;
  float fx=std::clamp((x-minX_)/(maxX_-minX_)*n_-0.5f,0.0f,float(n_-1)),fz=std::clamp((z-minZ_)/(maxZ_-minZ_)*n_-0.5f,0.0f,float(n_-1));
  int i=std::min(int(fx),n_-2),j=std::min(int(fz),n_-2); float tx=fx-i,tz=fz-j;
  auto g=[&](int a,int b){return signedDist_[size_t(b)*n_+a];};
  return (g(i,j)*(1-tx)+g(i+1,j)*tx)*(1-tz)+(g(i,j+1)*(1-tx)+g(i+1,j+1)*tx)*tz;
}
float EnvironmentMap::sampleChannel(int c,float x,float z) const {
  if(!ready()) return 0;
  float fx=std::clamp((x-minX_)/(maxX_-minX_)*n_-0.5f,0.0f,float(n_-1)),fz=std::clamp((z-minZ_)/(maxZ_-minZ_)*n_-0.5f,0.0f,float(n_-1));
  int i=std::min(int(fx),n_-2),j=std::min(int(fz),n_-2); float tx=fx-i,tz=fz-j;
  auto g=[&](int a,int b){return rgba_[(size_t(b)*n_+a)*4+c]/255.0f;};
  return (g(i,j)*(1-tx)+g(i+1,j)*tx)*(1-tz)+(g(i,j+1)*(1-tx)+g(i+1,j+1)*tx)*tz;
}
float EnvironmentMap::signedWaterDistance(float x,float z) const { return sampleDistance(x,z); }
float EnvironmentMap::waterDistance(float x,float z) const { return std::max(0.0f,sampleDistance(x,z)); }
bool EnvironmentMap::inWater(float x,float z) const { return sampleDistance(x,z)<=0; }
float EnvironmentMap::shore(float x,float z) const { return sampleChannel(0,x,z); }
float EnvironmentMap::wetness(float x,float z) const { return sampleChannel(1,x,z); }
float EnvironmentMap::riparian(float x,float z) const { return sampleChannel(2,x,z); }
float EnvironmentMap::cover(float x,float z) const { return sampleChannel(3,x,z); }
float EnvironmentMap::vegetationFactor(float x,float z) const {
  float d=sampleDistance(x,z); if(d<=0) return 0;
  return std::clamp(smooth(3,5.5f,d)*(1-.85f*shore(x,z))*(1+.6f*riparian(x,z)),0.0f,1.6f);
}
}
