/**
 * @file
 */

#pragma once

#include "video/Buffer.h"
#include "video/Types.h"
#include "video/Renderer.h"
#include "voxel/VoxelVertex.h"
#include "voxel/Constants.h"
#include "core/GLM.h"

namespace voxelrender {

inline video::Attribute getPositionVertexAttribute(uint32_t bufferIndex, uint32_t attributeLocation, int components = sizeof(voxel::VoxelVertex::position) / sizeof(decltype(voxel::VoxelVertex::position)::value_type)) {
	static_assert(voxel::MAX_TERRAIN_HEIGHT < 256, "Max terrain height exceeds the valid voxel positions");
	video::Attribute attrib;
	attrib.bufferIndex = bufferIndex;
	attrib.location = attributeLocation;
	attrib.stride = sizeof(voxel::VoxelVertex);
	attrib.size = components;
	attrib.type = video::mapType<decltype(voxel::VoxelVertex::position)::value_type>();
	attrib.offset = offsetof(voxel::VoxelVertex, position);
	return attrib;
}

/**
 * @note we are uploading multiple bytes at once here
 */
inline video::Attribute getInfoVertexAttribute(uint32_t bufferIndex, uint32_t attributeLocation, int components) {
	static_assert(sizeof(voxel::VoxelVertex::colorIndex) == sizeof(uint8_t), "Voxel color size doesn't match");
	static_assert(sizeof(voxel::VoxelVertex::ambientOcclusion) == sizeof(uint8_t), "AO type size doesn't match");
	static_assert(sizeof(voxel::VoxelVertex::face) == sizeof(uint8_t), "Material type size doesn't match");
	static_assert(offsetof(voxel::VoxelVertex, ambientOcclusion) < offsetof(voxel::VoxelVertex, colorIndex), "Layout change of VoxelVertex without change in upload");
	static_assert(offsetof(voxel::VoxelVertex, colorIndex) < offsetof(voxel::VoxelVertex, face), "Layout change of VoxelVertex without change in upload");
	video::Attribute attrib;
	attrib.bufferIndex = bufferIndex;
	attrib.location = attributeLocation;
	attrib.stride = sizeof(voxel::VoxelVertex);
	attrib.size = components;
	attrib.type = video::mapType<decltype(voxel::VoxelVertex::ambientOcclusion)>();
	attrib.typeIsInt = true;
	attrib.offset = offsetof(voxel::VoxelVertex, ambientOcclusion);
	return attrib;
}

inline video::Attribute getOffsetVertexAttribute(uint32_t bufferIndex, uint32_t attributeLocation, int components) {
	video::Attribute voxelAttributeOffsets;
	voxelAttributeOffsets.bufferIndex = bufferIndex;
	voxelAttributeOffsets.location = attributeLocation;
	voxelAttributeOffsets.stride = sizeof(glm::vec3);
	voxelAttributeOffsets.size = components;
	voxelAttributeOffsets.type = video::mapType<glm::vec3::value_type>();
	voxelAttributeOffsets.divisor = 1;
	voxelAttributeOffsets.offset = offsetof(glm::vec3, x);
	return voxelAttributeOffsets;
}

}
