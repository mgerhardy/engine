/**
 * @file
 */

#pragma once

#include "Mesh.h"
#include "Voxel.h"
#include "VoxelVertex.h"
#include "core/Common.h"
#include "core/Assert.h"
#include "core/NonCopyable.h"
#include "Region.h"
#include <vector>
#include <glm/fwd.hpp>
#include <glm/vec3.hpp>
#include <list>
#include "core/Trace.h"
#include "Face.h"

namespace voxel {

/**
 * This constant defines the maximum number of quads which can share a vertex in a cubic style mesh.
 *
 * We try to avoid duplicate vertices by checking whether a vertex has already been added at a given position.
 * However, it is possible that vertices have the same position but different materials. In this case, the
 * vertices are not true duplicates and both must be added to the mesh. As far as I can tell, it is possible to have
 * at most eight vertices with the same position but different materials. For example, this worst-case scenario
 * happens when we have a 2x2x2 group of voxels, all with different materials and some/all partially transparent.
 * The vertex position at the center of this group is then going to be used by all eight voxels all with different
 * materials.
 */
const uint32_t MaxVerticesPerPosition = 8;

/**
 * @section Data structures
 */

struct Quad {
	inline Quad(IndexType v0, IndexType v1, IndexType v2, IndexType v3) {
		vertices[0] = v0;
		vertices[1] = v1;
		vertices[2] = v2;
		vertices[3] = v3;
	}

	IndexType vertices[4];
};

struct VertexData {
	int32_t index;
	Voxel voxel;
	uint8_t ambientOcclusion;
};

class Array : public core::NonCopyable {
private:
	uint32_t _width;
	uint32_t _height;
	uint32_t _depth;
	VertexData* _elements;
public:
	Array(uint32_t width, uint32_t height, uint32_t depth) :
			_width(width), _height(height), _depth(depth) {
		_elements = (VertexData*)core_malloc(width * height * depth * sizeof(VertexData));
		clear();
	}

	~Array() {
		core_free(_elements);
	}

	void clear() {
		core_memset(_elements, 0x0, _width * _height * _depth * sizeof(VertexData));
	}

	inline VertexData& operator()(uint32_t x, uint32_t y, uint32_t z) {
		core_assert_msg(x < _width && y < _height && z < _depth, "Array access is out-of-range.");
		return _elements[z * _width * _height + y * _width + x];
	}

	void swap(Array& other) {
		std::swap(_elements, other._elements);
	}
};

/**
 * @brief Should be a list because random inserts which we need in @c performQuadMerging are O(1)
 */
typedef std::list<Quad> QuadList;
typedef std::vector<QuadList> QuadListVector;

/**
 * @section Surface extraction
 */

extern IndexType addVertex(bool reuseVertices, uint32_t uX, uint32_t uY, uint32_t uZ, FaceNames face, const Voxel& materialIn, Array& existingVertices,
		Mesh* meshCurrent, const VoxelType face1, const VoxelType face2, const VoxelType corner, const glm::ivec3& offset);

/**
 * @note Notice that the ambient occlusion is different for the vertices on the side than it is for the
 * vertices on the top and bottom. To fix this, we just need to pick a consistent orientation for
 * the quads. This can be done by comparing the ambient occlusion values for each quad and selecting
 * an appropriate orientation. Quad vertices must be sorted in clockwise order.
 */
SDL_FORCE_INLINE bool isQuadFlipped(const VoxelVertex& v00, const VoxelVertex& v01, const VoxelVertex& v10, const VoxelVertex& v11) {
	return v00.ambientOcclusion + v11.ambientOcclusion > v01.ambientOcclusion + v10.ambientOcclusion;
}

extern void meshify(Mesh* result, bool mergeQuads, QuadListVector& vecListQuads);

/**
 * The CubicSurfaceExtractor creates a mesh in which each voxel appears to be rendered as a cube
 * Introduction
 *  ------------
 *  Games such as Minecraft and Voxatron have a unique graphical style in which each voxel in the world appears to be rendered
 *  as a single cube. Actually rendering a cube for each voxel would be very expensive, but in practice the only faces which need
 *  to be drawn are those which lie on the boundary between solid and empty voxels. The CubicSurfaceExtractor can be used to create
 *  such a mesh from PolyVox volume data. As an example, images from Minecraft and Voxatron are shown below:
 *
 *  \image html MinecraftAndVoxatron.jpg
 *
 *  Before we get into the specifics of the CubicSurfaceExtractor, it is useful to understand the principles which apply to *all* PolyVox
 *  surface extractors and which are described in the Surface Extraction document (ADD LINK). From here on, it is assumed that you
 *  are familier with PolyVox regions and how they are used to limit surface extraction to a particular part of the volume. The
 *  principles of allowing dynamic terrain are also common to all surface extractors and are described here (ADD LINK).
 *
 *  Basic Operation
 *  ---------------
 *  At its core, the CubicSurfaceExtractor works by by looking at pairs of adjacent voxels and determining whether a quad should be
 *  placed between then. The most simple situation to imagine is a binary volume where every voxel is either solid or empty. In this
 *  case a quad should be generated whenever a solid voxel is next to an empty voxel as this represents part of the surface of the
 *  solid object. There is no need to generate a quad between two solid voxels (this quad would never be seen as it is inside the
 *  object) and there is no need to generate a quad between two empty voxels (there is no object here). PolyVox allows the principle
 *  to be extended far beyond such simple binary volumes but they provide a useful starting point for understanding how the algorithm
 *  works.
 *
 *  As an example, lets consider the part of a volume shown below. We are going to explain the principles in only two dimensions as
 *  this makes it much simpler to illustrate, so you will need to mentally extend the process into the third dimension. Hopefully you will
 *  find this intuitive. The diagram below shows a small part of a larger volume (as indicated by the voxel coordinates on the axes) which
 *  contains only solid and empty voxels represented by solid and hollow circles respectively. The region on which we are running the
 *  surface extractor is marked in pink, and for the purpose of this example it corresponds to the whole of the diagram.
 *
 *  \image html CubicSurfaceExtractor1.png
 *
 *  The output of the surface extractor is the mesh marked in red. As you can see, this forms a closed object which corresponds to the
 *  shape of the underlying voxel data.
 *
 *  Working with Regions
 *  --------------------
 *   So far the behaviour is easy to understand, but let's look at what happens when the extraction is limited to a particular region of
 *   the volume. The figure below shows the same data set as the previous figure, but the extraction region (still marked in pink) has
 *   been limited to 13 to 16 in x and 47 to 51 in y:
 *
 *  \image html CubicSurfaceExtractor2.png
 *
 *  As you can see, the extractor continues to generate a number of quads as indicated by the solid red lines. However, you can also see
 *  that the shape is no longer closed. This is because the solid voxels actually extend outside the region which is being processed, and
 *  so the extractor does not encounter a boundary between solid and empty voxels. Although this may initially appear problematic, the
 *  hole in the mesh does not actually matter because it will be hidden by the mesh corresponding to the region adjacent to it (see next
 *  diagram).
 *
 *  More interestingly, the diagram also contains a couple of dotted red lines lying on the bottom and right hand side of the extracted
 *  region. These are present to illustrate a common point of confusion, which is that *no quads are generated at this position even though
 *  it is a boundary between solid and empty voxels*. This is indeed somewhat counter intuitive but there is a rational reasaoning behind
 *  it.
 *  If you consider the dashed line on the righthand side of the extracted region, then it is clear that this lies on a boundary between
 *  solid and empty voxels and so we do need to create quads here. But what is not so clear is whether these quads should be assigned to
 *  the mesh which corresponds to the region in pink, or whether they should be assigned to the region to the right of it which is marked
 *  in blue in the diagram below:
 *
 *  \image html CubicSurfaceExtractor3.png
 *
 *  We could choose to add the quads to *both* regions, but this can cause confusion when one of the region is modified (causing the face
 *  to disappear or a new one to be created) as *both* regions need to have their mesh regenerated to correctly represent the new state of
 *  the volume data. Such pairs of coplanar quads can also cause problems with physics engines, and may prevent transparent voxels from
 *  rendering correctly. Therefore we choose to instead only add the quad to one of the the regions and we always choose the one with the
 *  greater coordinate value in the direction in which they differ. In the above example the regions differ by the 'x' component of their
 *  position, and so the quad is added to the region with the greater 'x' value (the one marked in blue).
 *
 *  **Note:** *This behaviour has changed recently (September 2012). Earlier versions of PolyVox tried to be smart about this problem by
 *  looking beyond the region which was being processed, but this complicated the code and didn't work very well. Ultimatly we decided to
 *  simply stick with the convention outlined above.*
 *
 *  One of the practical implications of this is that when you modify a voxel *you may have to re-extract the mesh for regions other than
 *  region which actually contains the voxel you modified.* This happens when the voxel lies on the upper x,y or z face of a region.
 *  Assuming that you have some management code which can mark a region as needing re-extraction when a voxel changes, you should probably
 *  extend this to mark the regions of neighbouring voxels as invalid (this will have no effect when the voxel is well within a region,
 *  but will mark the neighbouring region as needing an update if the voxel lies on a region face).
 *
 *  Another scenario which sometimes results in confusion is when you wish to extract a region which corresponds to the whole volume,
 *  particularly when solid voxels extend right to the edge of the volume.
 *
 * This version of the function performs the extraction into a user-provided mesh rather than allocating a mesh automatically.
 * There are a few reasons why this might be useful to more advanced users:
 *
 *    1. It leaves the user in control of memory allocation and would allow them to implement e.g. a mesh pooling system.
 *    2. The user-provided mesh could have a different index type (e.g. 16-bit indices) to reduce memory usage.
 *    3. The user could provide a custom mesh class, e.g a thin wrapper around an openGL VBO to allow direct writing into this structure.
 */
template<typename VolumeType, typename IsQuadNeeded>
void extractCubicMesh(VolumeType* volData, const Region& region, Mesh* result, IsQuadNeeded isQuadNeeded, bool mergeQuads = true, bool reuseVertices = true) {
	core_trace_scoped(ExtractCubicMesh);

	result->clear();
	const glm::ivec3& offset = region.getLowerCorner();
	const glm::ivec3& upper = region.getUpperCorner();
	result->setOffset(offset);

	// Used to avoid creating duplicate vertices.
	const int widthInCells = upper.x - offset.x;
	const int heightInCells = upper.y - offset.y;
	Array previousSliceVertices(widthInCells + 2, heightInCells + 2, MaxVerticesPerPosition);
	Array currentSliceVertices(widthInCells + 2, heightInCells + 2, MaxVerticesPerPosition);

	// During extraction we create a number of different lists of quads. All the
	// quads in a given list are in the same plane and facing in the same direction.
	QuadListVector vecQuads[NoOfFaces];

	const int xSize = upper.x - offset.x + 2;
	const int ySize = upper.y - offset.y + 2;
	const int zSize = upper.z - offset.z + 2;
	vecQuads[NegativeX].resize(xSize);
	vecQuads[PositiveX].resize(xSize);

	vecQuads[NegativeY].resize(ySize);
	vecQuads[PositiveY].resize(ySize);

	vecQuads[NegativeZ].resize(zSize);
	vecQuads[PositiveZ].resize(zSize);

	typename VolumeType::Sampler volumeSampler(volData);

	{
	core_trace_scoped(QuadGeneration);
	for (int32_t z = offset.z; z <= upper.z; ++z) {
		const uint32_t regZ = z - offset.z;

		for (int32_t y = offset.y; y <= upper.y; ++y) {
			const uint32_t regY = y - offset.y;

			volumeSampler.setPosition(offset.x, y, z);

			for (int32_t x = offset.x; x <= upper.x; ++x) {
				const uint32_t regX = x - offset.x;

				/**
				 *
				 *
				 *                  [D]
				 *            8 ____________ 7
				 *             /|          /|
				 *            / |         / |              ABOVE [D] |
				 *           /  |    [F] /  |              BELOW [C]
				 *        5 /___|_______/ 6 |  [B]       y           BEHIND  [F]
				 *    [A]   |   |_______|___|              |      z  BEFORE [E] /
				 *          | 4 /       |   / 3            |   /
				 *          |  / [E]    |  /               |  /   . center
				 *          | /         | /                | /
				 *          |/__________|/                 |/________   LEFT  RIGHT
				 *        1               2                          x   [A] - [B]
				 *               [C]
				 */

				const Voxel& voxelCurrent          = volumeSampler.voxel();
				const Voxel& voxelLeft             = volumeSampler.peekVoxel1nx0py0pz();
				const Voxel& voxelBefore           = volumeSampler.peekVoxel0px0py1nz();
				const Voxel& voxelLeftBefore       = volumeSampler.peekVoxel1nx0py1nz();
				const Voxel& voxelRightBefore      = volumeSampler.peekVoxel1px0py1nz();
				const Voxel& voxelLeftBehind       = volumeSampler.peekVoxel1nx0py1pz();

				const Voxel& voxelAboveLeft        = volumeSampler.peekVoxel1nx1py0pz();
				const Voxel& voxelAboveBefore      = volumeSampler.peekVoxel0px1py1nz();
				const Voxel& voxelAboveLeftBefore  = volumeSampler.peekVoxel1nx1py1nz();
				const Voxel& voxelAboveRightBefore = volumeSampler.peekVoxel1px1py1nz();
				const Voxel& voxelAboveLeftBehind  = volumeSampler.peekVoxel1nx1py1pz();

				const Voxel& voxelBelow            = volumeSampler.peekVoxel0px1ny0pz();
				const Voxel& voxelBelowLeft        = volumeSampler.peekVoxel1nx1ny0pz();
				const Voxel& voxelBelowBefore      = volumeSampler.peekVoxel0px1ny1nz();
				const Voxel& voxelBelowLeftBefore  = volumeSampler.peekVoxel1nx1ny1nz();
				const Voxel& voxelBelowRightBefore = volumeSampler.peekVoxel1px1ny1nz();
				const Voxel& voxelBelowLeftBehind  = volumeSampler.peekVoxel1nx1ny1pz();

				const VoxelType voxelCurrentMaterial          = voxelCurrent.getMaterial();
				const VoxelType voxelLeftMaterial             = voxelLeft.getMaterial();
				const VoxelType voxelBelowMaterial            = voxelBelow.getMaterial();
				const VoxelType voxelBeforeMaterial           = voxelBefore.getMaterial();
				const VoxelType voxelLeftBeforeMaterial       = voxelLeftBefore.getMaterial();
				const VoxelType voxelBelowLeftMaterial        = voxelBelowLeft.getMaterial();
				const VoxelType voxelBelowLeftBeforeMaterial  = voxelBelowLeftBefore.getMaterial();
				const VoxelType voxelLeftBehindMaterial       = voxelLeftBehind.getMaterial();
				const VoxelType voxelBelowLeftBehindMaterial  = voxelBelowLeftBehind.getMaterial();
				const VoxelType voxelAboveLeftMaterial        = voxelAboveLeft.getMaterial();
				const VoxelType voxelAboveLeftBehindMaterial  = voxelAboveLeftBehind.getMaterial();
				const VoxelType voxelAboveLeftBeforeMaterial  = voxelAboveLeftBefore.getMaterial();

				// X [A] LEFT
				if (isQuadNeeded(voxelCurrentMaterial, voxelLeftMaterial, NegativeX)) {
					const IndexType v_0_1 = addVertex(reuseVertices, regX, regY,     regZ,     NegativeX, voxelCurrent, previousSliceVertices, result,
							voxelLeftBeforeMaterial, voxelBelowLeftMaterial, voxelBelowLeftBeforeMaterial, offset);
					const IndexType v_1_4 = addVertex(reuseVertices, regX, regY,     regZ + 1, NegativeX, voxelCurrent, currentSliceVertices,  result,
							voxelBelowLeftMaterial, voxelLeftBehindMaterial, voxelBelowLeftBehindMaterial, offset);
					const IndexType v_2_8 = addVertex(reuseVertices, regX, regY + 1, regZ + 1, NegativeX, voxelCurrent, currentSliceVertices,  result,
							voxelLeftBehindMaterial, voxelAboveLeftMaterial, voxelAboveLeftBehindMaterial, offset);
					const IndexType v_3_5 = addVertex(reuseVertices, regX, regY + 1, regZ,     NegativeX, voxelCurrent, previousSliceVertices, result,
							voxelAboveLeftMaterial, voxelLeftBeforeMaterial, voxelAboveLeftBeforeMaterial, offset);
					vecQuads[NegativeX][regX].emplace_back(v_0_1, v_1_4, v_2_8, v_3_5);
				}

				// X [B] RIGHT
				if (isQuadNeeded(voxelLeftMaterial, voxelCurrentMaterial, PositiveX)) {
					volumeSampler.moveNegativeX();

					const VoxelType _voxelRightBefore      = volumeSampler.peekVoxel1px0py1nz().getMaterial();
					const VoxelType _voxelRightBehind      = volumeSampler.peekVoxel1px0py1pz().getMaterial();

					const VoxelType _voxelAboveRight       = volumeSampler.peekVoxel1px1py0pz().getMaterial();
					const VoxelType _voxelAboveRightBefore = volumeSampler.peekVoxel1px1py1nz().getMaterial();
					const VoxelType _voxelAboveRightBehind = volumeSampler.peekVoxel1px1py1pz().getMaterial();

					const VoxelType _voxelBelowRight       = volumeSampler.peekVoxel1px1ny0pz().getMaterial();
					const VoxelType _voxelBelowRightBefore = volumeSampler.peekVoxel1px1ny1nz().getMaterial();
					const VoxelType _voxelBelowRightBehind = volumeSampler.peekVoxel1px1ny1pz().getMaterial();

					const IndexType v_0_2 = addVertex(reuseVertices, regX, regY,     regZ,     PositiveX, voxelLeft, previousSliceVertices, result,
							_voxelBelowRight, _voxelRightBefore, _voxelBelowRightBefore, offset);
					const IndexType v_1_3 = addVertex(reuseVertices, regX, regY,     regZ + 1, PositiveX, voxelLeft, currentSliceVertices,  result,
							_voxelBelowRight, _voxelRightBehind, _voxelBelowRightBehind, offset);
					const IndexType v_2_7 = addVertex(reuseVertices, regX, regY + 1, regZ + 1, PositiveX, voxelLeft, currentSliceVertices,  result,
							_voxelAboveRight, _voxelRightBehind, _voxelAboveRightBehind, offset);
					const IndexType v_3_6 = addVertex(reuseVertices, regX, regY + 1, regZ,     PositiveX, voxelLeft, previousSliceVertices, result,
							_voxelAboveRight, _voxelRightBefore, _voxelAboveRightBefore, offset);
					vecQuads[PositiveX][regX].emplace_back(v_0_2, v_3_6, v_2_7, v_1_3);

					volumeSampler.movePositiveX();
				}

				// Y [C] BELOW
				if (isQuadNeeded(voxelCurrentMaterial, voxelBelowMaterial, NegativeY)) {
					const Voxel& voxelBelowRightBehind = volumeSampler.peekVoxel1px1ny1pz();
					const Voxel& voxelBelowRight       = volumeSampler.peekVoxel1px1ny0pz();
					const Voxel& voxelBelowBehind      = volumeSampler.peekVoxel0px1ny1pz();

					const VoxelType voxelBelowRightMaterial       = voxelBelowRight.getMaterial();
					const VoxelType voxelBelowBeforeMaterial      = voxelBelowBefore.getMaterial();
					const VoxelType voxelBelowRightBeforeMaterial = voxelBelowRightBefore.getMaterial();
					const VoxelType voxelBelowBehindMaterial      = voxelBelowBehind.getMaterial();
					const VoxelType voxelBelowRightBehindMaterial = voxelBelowRightBehind.getMaterial();
					const IndexType v_0_1 = addVertex(reuseVertices, regX,     regY, regZ,     NegativeY, voxelCurrent, previousSliceVertices, result,
							voxelBelowBeforeMaterial, voxelBelowLeftMaterial, voxelBelowLeftBeforeMaterial, offset);
					const IndexType v_1_2 = addVertex(reuseVertices, regX + 1, regY, regZ,     NegativeY, voxelCurrent, previousSliceVertices, result,
							voxelBelowRightMaterial, voxelBelowBeforeMaterial, voxelBelowRightBeforeMaterial, offset);
					const IndexType v_2_3 = addVertex(reuseVertices, regX + 1, regY, regZ + 1, NegativeY, voxelCurrent, currentSliceVertices,  result,
							voxelBelowBehindMaterial, voxelBelowRightMaterial, voxelBelowRightBehindMaterial, offset);
					const IndexType v_3_4 = addVertex(reuseVertices, regX,     regY, regZ + 1, NegativeY, voxelCurrent, currentSliceVertices,  result,
							voxelBelowLeftMaterial, voxelBelowBehindMaterial, voxelBelowLeftBehindMaterial, offset);
					vecQuads[NegativeY][regY].emplace_back(v_0_1, v_1_2, v_2_3, v_3_4);
				}

				// Y [D] ABOVE
				if (isQuadNeeded(voxelBelowMaterial, voxelCurrentMaterial, PositiveY)) {
					volumeSampler.moveNegativeY();

					const VoxelType _voxelAboveLeft        = volumeSampler.peekVoxel1nx1py0pz().getMaterial();
					const VoxelType _voxelAboveRight       = volumeSampler.peekVoxel1px1py0pz().getMaterial();
					const VoxelType _voxelAboveBefore      = volumeSampler.peekVoxel0px1py1nz().getMaterial();
					const VoxelType _voxelAboveBehind      = volumeSampler.peekVoxel0px1py1pz().getMaterial();
					const VoxelType _voxelAboveLeftBefore  = volumeSampler.peekVoxel1nx1py1nz().getMaterial();
					const VoxelType _voxelAboveRightBefore = volumeSampler.peekVoxel1px1py1nz().getMaterial();
					const VoxelType _voxelAboveLeftBehind  = volumeSampler.peekVoxel1nx1py1pz().getMaterial();
					const VoxelType _voxelAboveRightBehind = volumeSampler.peekVoxel1px1py1pz().getMaterial();

					const IndexType v_0_5 = addVertex(reuseVertices, regX,     regY, regZ,     PositiveY, voxelBelow, previousSliceVertices, result,
							_voxelAboveBefore, _voxelAboveLeft, _voxelAboveLeftBefore, offset);
					const IndexType v_1_6 = addVertex(reuseVertices, regX + 1, regY, regZ,     PositiveY, voxelBelow, previousSliceVertices, result,
							_voxelAboveRight, _voxelAboveBefore, _voxelAboveRightBefore, offset);
					const IndexType v_2_7 = addVertex(reuseVertices, regX + 1, regY, regZ + 1, PositiveY, voxelBelow, currentSliceVertices,  result,
							_voxelAboveBehind, _voxelAboveRight, _voxelAboveRightBehind, offset);
					const IndexType v_3_8 = addVertex(reuseVertices, regX,     regY, regZ + 1, PositiveY, voxelBelow, currentSliceVertices,  result,
							_voxelAboveLeft, _voxelAboveBehind, _voxelAboveLeftBehind, offset);
					vecQuads[PositiveY][regY].emplace_back(v_0_5, v_3_8, v_2_7, v_1_6);

					volumeSampler.movePositiveY();
				}

				// Z [E] BEFORE
				if (isQuadNeeded(voxelCurrentMaterial, voxelBeforeMaterial, NegativeZ)) {
					const VoxelType voxelBelowBeforeMaterial = voxelBelowBefore.getMaterial();
					const VoxelType voxelAboveBeforeMaterial = voxelAboveBefore.getMaterial();
					const VoxelType voxelRightBeforeMaterial = voxelRightBefore.getMaterial();
					const VoxelType voxelAboveRightBeforeMaterial = voxelAboveRightBefore.getMaterial();
					const VoxelType voxelBelowRightBeforeMaterial = voxelBelowRightBefore.getMaterial();

					const IndexType v_0_1 = addVertex(reuseVertices, regX,     regY,     regZ, NegativeZ, voxelCurrent, previousSliceVertices, result,
							voxelBelowBeforeMaterial, voxelLeftBeforeMaterial, voxelBelowLeftBeforeMaterial, offset); //1
					const IndexType v_1_5 = addVertex(reuseVertices, regX,     regY + 1, regZ, NegativeZ, voxelCurrent, previousSliceVertices, result,
							voxelAboveBeforeMaterial, voxelLeftBeforeMaterial, voxelAboveLeftBeforeMaterial, offset); //5
					const IndexType v_2_6 = addVertex(reuseVertices, regX + 1, regY + 1, regZ, NegativeZ, voxelCurrent, previousSliceVertices, result,
							voxelAboveBeforeMaterial, voxelRightBeforeMaterial, voxelAboveRightBeforeMaterial, offset); //6
					const IndexType v_3_2 = addVertex(reuseVertices, regX + 1, regY,     regZ, NegativeZ, voxelCurrent, previousSliceVertices, result,
							voxelBelowBeforeMaterial, voxelRightBeforeMaterial, voxelBelowRightBeforeMaterial, offset); //2
					vecQuads[NegativeZ][regZ].emplace_back(v_0_1, v_1_5, v_2_6, v_3_2);
				}

				// Z [F] BEHIND
				if (isQuadNeeded(voxelBeforeMaterial, voxelCurrentMaterial, PositiveZ)) {
					volumeSampler.moveNegativeZ();

					const VoxelType _voxelLeftBehind       = volumeSampler.peekVoxel1nx0py1pz().getMaterial();
					const VoxelType _voxelRightBehind      = volumeSampler.peekVoxel1px0py1pz().getMaterial();

					const VoxelType _voxelAboveBehind      = volumeSampler.peekVoxel0px1py1pz().getMaterial();
					const VoxelType _voxelAboveLeftBehind  = volumeSampler.peekVoxel1nx1py1pz().getMaterial();
					const VoxelType _voxelAboveRightBehind = volumeSampler.peekVoxel1px1py1pz().getMaterial();

					const VoxelType _voxelBelowBehind      = volumeSampler.peekVoxel0px1ny1pz().getMaterial();
					const VoxelType _voxelBelowLeftBehind  = volumeSampler.peekVoxel1nx1ny1pz().getMaterial();
					const VoxelType _voxelBelowRightBehind = volumeSampler.peekVoxel1px1ny1pz().getMaterial();

					const IndexType v_0_4 = addVertex(reuseVertices, regX,     regY,     regZ, PositiveZ, voxelBefore, previousSliceVertices, result,
							_voxelBelowBehind, _voxelLeftBehind, _voxelBelowLeftBehind, offset); //4
					const IndexType v_1_8 = addVertex(reuseVertices, regX,     regY + 1, regZ, PositiveZ, voxelBefore, previousSliceVertices, result,
							_voxelAboveBehind, _voxelLeftBehind, _voxelAboveLeftBehind, offset); //8
					const IndexType v_2_7 = addVertex(reuseVertices, regX + 1, regY + 1, regZ, PositiveZ, voxelBefore, previousSliceVertices, result,
							_voxelAboveBehind, _voxelRightBehind, _voxelAboveRightBehind, offset); //7
					const IndexType v_3_3 = addVertex(reuseVertices, regX + 1, regY,     regZ, PositiveZ, voxelBefore, previousSliceVertices, result,
							_voxelBelowBehind, _voxelRightBehind, _voxelBelowRightBehind, offset); //3
					vecQuads[PositiveZ][regZ].emplace_back(v_0_4, v_3_3, v_2_7, v_1_8);

					volumeSampler.movePositiveZ();
				}

				if (core_likely(x != upper.x)) {
					volumeSampler.movePositiveX();
				}
			}
		}

		previousSliceVertices.swap(currentSliceVertices);
		currentSliceVertices.clear();
	}
	}

	{
		core_trace_scoped(GenerateMesh);
		for (QuadListVector& vecListQuads : vecQuads) {
			meshify(result, mergeQuads, vecListQuads);
		}
	}

	result->removeUnusedVertices();
}

}

#undef BUFFERED_SAMPLER
