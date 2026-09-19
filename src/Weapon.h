#pragma once

#include "ObjectModel.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <vector>
#include <map>
#include <string>

struct PoseOffsets {
  glm::vec3 positionOffset = glm::vec3(0.0f);
  glm::vec3 rotationOffset = glm::vec3(0.0f);
};

struct Weapon {
  ObjModel *model = nullptr;
  glm::vec3 positionOffset = glm::vec3(0.0f);
  glm::vec3 rotationOffset = glm::vec3(0.0f); // degrees: x=pitch, y=yaw, z=roll
  glm::vec3 scaleOffset = glm::vec3(1.0f);
  int handBoneID = -1;

  // Weapon Recoil and Properties
  std::string name = "MP40";
  float fireRate = 0.1f;               // Delay between shots in seconds
  float recoilPitch = 1.0f;            // Vertical climb per shot (degrees)
  float recoilYawMin = -0.3f;          // Minimum horizontal sway per shot (degrees)
  float recoilYawMax = 0.3f;           // Maximum horizontal sway per shot (degrees)
  float recoilRecovery = 5.0f;         // Speed of returning to base aim (degrees/sec)
  float visualRecoilMult = 1.0f;       // Visual screen bounce intensity multiplier
  int ammo = 30;                       // Magazine capacity
  int currentAmmo = 30;                // Current bullets in magazine
  bool automatic = true;               // True if the weapon fires continuously while held
  glm::vec3 muzzleOffset = glm::vec3(0.0f, 0.1f, 0.5f); // Local offset to the gun barrel (for flash/tracers)

  std::map<std::string, PoseOffsets> poseOffsets;

  glm::vec3 getPositionOffsetForPose(const std::string& pose) const {
    auto it = poseOffsets.find(pose);
    if (it != poseOffsets.end()) {
      return it->second.positionOffset;
    }
    return positionOffset;
  }

  glm::vec3 getRotationOffsetForPose(const std::string& pose) const {
    auto it = poseOffsets.find(pose);
    if (it != poseOffsets.end()) {
      return it->second.rotationOffset;
    }
    return rotationOffset;
  }
};

class WeaponManager {
public:
  std::vector<Weapon> weapons;
  int currentWeapon = -1;

  void addWeapon(const Weapon &weapon) {
    weapons.push_back(weapon);
    if (currentWeapon == -1) {
      currentWeapon = 0;
    }
  }

  Weapon *getCurrentWeapon() {
    if (currentWeapon >= 0 && currentWeapon < (int)weapons.size()) {
      return &weapons[currentWeapon];
    }
    return nullptr;
  }

  void switchWeapon(int index) {
    if (index >= 0 && index < (int)weapons.size()) {
      currentWeapon = index;
    }
  }

  void cycleWeapon() {
    if (!weapons.empty()) {
      currentWeapon = (currentWeapon + 1) % weapons.size();
    }
  }

  glm::mat4 createWeaponOffset(const Weapon &weapon, const std::string& pose = "Idle") {
    glm::mat4 offset = glm::mat4(1.0f);
    glm::vec3 pos = weapon.getPositionOffsetForPose(pose);
    glm::vec3 rot = weapon.getRotationOffsetForPose(pose);

    offset = glm::translate(offset, pos);

    offset *= glm::yawPitchRoll(glm::radians(rot.y),
                                glm::radians(rot.x + 90),
                                glm::radians(rot.z));

    offset = glm::scale(offset, weapon.scaleOffset);
    return offset;
  }

  ~WeaponManager() {
    for (auto &w : weapons) {
      delete w.model;
    }
  }
};
