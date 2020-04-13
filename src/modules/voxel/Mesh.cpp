/**
 * @file
 */

#include "Mesh.h"
#include "CubicSurfaceExtractor.h"
#include "core/Common.h"
#include "core/Trace.h"
#include "core/Assert.h"
#include <glm/vector_relational.hpp>
#include <glm/common.hpp>

namespace voxel {

Mesh::Mesh(int vertices, int indices, bool mayGetResized) : _mayGetResized(mayGetResized) {
	if (vertices > 0) {
		_vecVertices.reserve(vertices);
	}
	if (indices > 0) {
		_vecIndices.reserve(indices);
	}
}

const IndexArray& Mesh::getIndexVector() const {
	return _vecIndices;
}

const VertexArray& Mesh::getVertexVector() const {
	return _vecVertices;
}

IndexArray& Mesh::getIndexVector() {
	return _vecIndices;
}

VertexArray& Mesh::getVertexVector() {
	return _vecVertices;
}

size_t Mesh::getNoOfVertices() const {
	return _vecVertices.size();
}

const VoxelVertex& Mesh::getVertex(IndexType index) const {
	return _vecVertices[index];
}

const VoxelVertex* Mesh::getRawVertexData() const {
	return _vecVertices.data();
}

size_t Mesh::getNoOfIndices() const {
	return _vecIndices.size();
}

IndexType Mesh::getIndex(IndexType index) const {
	return _vecIndices[index];
}

const IndexType* Mesh::getRawIndexData() const {
	return _vecIndices.data();
}

const glm::ivec3& Mesh::getOffset() const {
	return _offset;
}

void Mesh::setOffset(const glm::ivec3& offset) {
	_offset = offset;
}

void Mesh::clear() {
	_vecVertices.clear();
	_vecIndices.clear();
	_offset = glm::ivec3(0);
}

bool Mesh::isEmpty() const {
	return getNoOfVertices() == 0 || getNoOfIndices() == 0;
}

void Mesh::addTriangle(IndexType index0, IndexType index1, IndexType index2) {
	//Make sure the specified indices correspond to valid vertices.
	core_assert_msg(index0 < _vecVertices.size(), "Index points at an invalid vertex.");
	core_assert_msg(index1 < _vecVertices.size(), "Index points at an invalid vertex.");
	core_assert_msg(index2 < _vecVertices.size(), "Index points at an invalid vertex.");
	if (!_mayGetResized) {
		core_assert_msg(_vecIndices.size() + 3 < _vecIndices.capacity(), "addTriangle() call exceeds the capacity of the indices vector and will trigger a realloc (%i vs %i)", (int)_vecIndices.size(), (int)_vecIndices.capacity());
	}

	_vecIndices.push_back(index0);
	_vecIndices.push_back(index1);
	_vecIndices.push_back(index2);
}

IndexType Mesh::addVertex(const VoxelVertex& vertex) {
	// We should not add more vertices than our chosen index type will let us index.
	core_assert_msg(_vecVertices.size() < (std::numeric_limits<IndexType>::max)(), "Mesh has more vertices that the chosen index type allows.");
	if (!_mayGetResized) {
		core_assert_msg(_vecVertices.size() + 1 < _vecVertices.capacity(), "addVertex() call exceeds the capacity of the vertices vector and will trigger a realloc (%i vs %i)", (int)_vecVertices.size(), (int)_vecVertices.capacity());
	}

	_vecVertices.push_back(vertex);
	return (IndexType)_vecVertices.size() - 1;
}

size_t Mesh::size() {
	constexpr size_t classSize = sizeof(*this);
	const size_t indicesSize = _vecIndices.size() * sizeof(IndexType);
	const size_t verticesSize = _vecVertices.size() * sizeof(VoxelVertex);
	const size_t contentSize = indicesSize + verticesSize;
	return classSize + contentSize;
}

void Mesh::removeUnusedVertices() {
	const size_t vertices = _vecVertices.size();
	const size_t indices = _vecIndices.size();
	std::vector<bool> isVertexUsed(vertices);
	std::fill(isVertexUsed.begin(), isVertexUsed.end(), false);

	for (size_t triCt = 0u; triCt < indices; ++triCt) {
		IndexType v = _vecIndices[triCt];
		isVertexUsed[v] = true;
	}

	_mins = glm::ivec3((std::numeric_limits<int>::max)());
	_maxs = glm::ivec3((std::numeric_limits<int>::min)());

	int noOfUsedVertices = 0;
	IndexArray newPos(vertices);
	for (size_t vertCt = 0u; vertCt < vertices; ++vertCt) {
		if (!isVertexUsed[vertCt]) {
			continue;
		}
		const VoxelVertex& v = _vecVertices[vertCt];
		_vecVertices[noOfUsedVertices] = v;
		_mins = (glm::min)(_mins, v.position);
		_maxs = (glm::max)(_maxs, v.position);
		newPos[vertCt] = noOfUsedVertices;
		++noOfUsedVertices;
	}

	_vecVertices.resize(noOfUsedVertices);

	for (size_t triCt = 0u; triCt < indices; ++triCt) {
		_vecIndices[triCt] = newPos[_vecIndices[triCt]];
	}
	_vecIndices.resize(indices);
}

bool Mesh::operator<(const Mesh& rhs) const {
	return glm::all(glm::lessThan(getOffset(), rhs.getOffset()));
}

}
