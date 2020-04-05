/**
 * @file
 */

#pragma once

#include "Voxel.h"
#include <glm/vec3.hpp>

namespace voxel {

/**
 * @brief Represents a vertex in a mesh and includes position and ambient occlusion
 * as well as color and material information.
 */
struct VoxelVertex {
	glm::ivec3 position;
	/**
	 * AO: 0 is the darkest, 3 is no occlusion at all (2 bits)
	 */
	uint8_t ambientOcclusion;
	uint8_t colorIndex;
	/**
	 * Face: 0-5 (3 bits)
	 * @see voxel::FaceNames
	 */
	uint8_t face;
	uint8_t padding;
};
static_assert(sizeof(VoxelVertex) == 16, "Unexpected size of the vertex struct");

// TODO: maybe reduce to uint16_t and use glDrawElementsBaseVertex
typedef uint32_t IndexType;

}
