/**
 * @file
 */

#pragma once

#include <glm/fwd.hpp>

namespace voxel {

enum FaceNames {
	PositiveX = 0, PositiveY = 1, PositiveZ = 2, NegativeX = 3, NegativeY = 4, NegativeZ = 5, NoOfFaces
};

inline bool isHorizontalFace(FaceNames face) {
	return face == PositiveX || face == PositiveZ || face == NegativeX || face == NegativeZ;
}

inline bool isVerticalFace(FaceNames face) {
	 return face == PositiveY || face == NegativeY;
}

extern FaceNames raycastFaceDetection(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::ivec3& hitPos);

}
