/*
MIT License

https://github.com/cgerikj/binary-greedy-meshing

Copyright (c) 2020 Erik Johansson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

#pragma once

#include "Voxel.h"
#include <glm/glm.hpp>
#include <stdint.h>
#include <vector>

using Voxels = std::vector<Voxel>;
using Vertex = uint32_t;
using Vertices = std::vector<Vertex>;

// CS = chunk size (max 62)
static constexpr int CS = 62;
static constexpr int Y_CHUNKS = 16;

// Padded chunk size
static constexpr int CS_P = CS + 2;
static constexpr int CS_P2 = CS_P * CS_P;
static constexpr int CS_P3 = CS_P * CS_P * CS_P;

#ifdef _MSC_VER
static inline const int CTZ(uint64_t &x) {
	unsigned long index;
	_BitScanForward64(&index, x);
	return (int)index;
}
#else
static inline const int CTZ(uint64_t x) {
	return __builtin_ctzll(x);
}
#endif

static inline constexpr int get_axis_i(const int &axis, const int &a, const int &b, const int &c) {
	if (axis == 0) {
		return b + (a * CS_P) + (c * CS_P2);
	} else if (axis == 1) {
		return a + (c * CS_P) + (b * CS_P2);
	}
	return c + (b * CS_P) + (a * CS_P2);
}

// Add checks to this function to skip culling against grass for example
static inline constexpr bool solid_check(Voxel voxel) {
	return voxel > 0;
}

static constexpr glm::ivec2 ao_dirs[8] = {
	glm::ivec2(0, -1),	glm::ivec2(0, 1),  glm::ivec2(-1, 0), glm::ivec2(1, 0),
	glm::ivec2(-1, -1), glm::ivec2(-1, 1), glm::ivec2(1, -1), glm::ivec2(1, 1),
};

static inline constexpr int vertexAO(int side1, int side2, int corner) {
	if (side1 && side2) {
		return 0;
	}
	return 3 - (side1 + side2 + corner);
}

static inline const bool compare_ao(Voxels &voxels, int axis, int forward, int right, int c, int forward_offset, int right_offset) {
	for (const auto &ao_dir : ao_dirs) {
		if (solid_check(voxels.at(get_axis_i(axis, right + ao_dir[0], forward + ao_dir[1], c))) !=
			solid_check(voxels.at(get_axis_i(axis, right + right_offset + ao_dir[0], forward + forward_offset + ao_dir[1], c)))) {
			return false;
		}
	}
	return true;
}

static inline const bool compare_forward(Voxels &voxels, int axis, int forward, int right, int bit_pos) {
	return voxels.at(get_axis_i(axis, right, forward, bit_pos)) == voxels.at(get_axis_i(axis, right, forward + 1, bit_pos)) &&
		   compare_ao(voxels, axis, forward, right, bit_po, 1, 0);
}

static inline const bool compare_right(Voxels &voxels, int axis, int forward, int right, int bit_pos) {
	return voxels.at(get_axis_i(axis, right, forward, bit_pos)) == voxels.at(get_axis_i(axis, right + 1, forward, bit_pos)) &&
		   compare_ao(voxels, axis, forward, right, bit_pos, 0, 1);
}

static inline const void insert_quad(Vertices *vertices, Vertex v1, Vertex v2, Vertex v3, Vertex v4, bool flipped) {
	if (flipped) {
		vertices->insert(vertices->end(), {v1, v2, v4, v4, v2, v3});
	} else {
		vertices->insert(vertices->end(), {v1, v2, v3, v3, v4, v1});
	}
}

static inline constexpr Vertex get_vertex(uint32_t x, uint32_t y, uint32_t z, Voxel type, uint32_t norm, uint32_t ao) {
	return (ao << 30) | (norm << 27) | (type << 23) | ((z - 1) << 12) | ((y - 1) << 6) | (x - 1);
}

/**
 * @return array index for putting voxels into the right index
 */
constexpr int get_yzx_index(int x, int y, int z) {
	return z + (x * CS_P) + (y * CS_P2);
}

/**
 * Fast voxel meshing algorithm - creates 'greedy' meshes with support for voxel types, ambient occlusion.
 *
 * @param[in] voxels 64^3 (includes neighboring voxels) (values 0-31 usable)
 * @note The input data includes duplicate edge data from neighboring chunks which is used for visibility
 * culling and AO. Input data is ordered in YXZ and is 64^3 which results in a 62^3 mesh.
 *
 * Vertex data is packed into one unsigned integer:
 * @li x, y, z: 6 bit each (0-63)
 * @li Type: 9 bit (0-511)
 * @li Normal: 3 bit (0-5)
 * @li AO: 2 bit
 *
 * Meshes can be offset to world space using a per-draw uniform or by packing xyz in @c gl_BaseInstance
 * if rendering with @c glMultiDrawArraysIndirect.
 */
void mesh(const Voxels &voxels, Vertices* vertices) {
	core_assert(voxels.size() == CS_P3);
	uint64_t axis_cols[CS_P2 * 3] = {0};
	uint64_t col_face_masks[CS_P2 * 6];

	// Step 1: Convert to binary representation for each direction
	auto p = voxels.begin();
	for (int y = 0; y < CS_P; ++y) {
		for (int x = 0; x < CS_P; ++x) {
			uint64_t zb = 0;
			for (int z = 0; z < CS_P; ++z) {
				if (solid_check(*p)) {
					axis_cols[x + (z * CS_P)] |= 1ULL << y;
					axis_cols[z + (y * CS_P) + (CS_P2)] |= 1ULL << x;
					zb |= 1ULL << z;
				}
				++p;
			}
			axis_cols[y + (x * CS_P) + (CS_P2 * 2)] = zb;
		}
	}

	// Step 2: Visible face culling
	for (int axis = 0; axis <= 2; ++axis) {
		for (int i = 0; i < CS_P2; ++i) {
			const uint64_t col = axis_cols[(CS_P2 * axis) + i];
			col_face_masks[(CS_P2 * (axis * 2)) + i] = col & ~((col >> 1) | (1ULL << (CS_P - 1)));
			col_face_masks[(CS_P2 * (axis * 2 + 1)) + i] = col & ~((col << 1) | 1ULL);
		}
	}

	// Step 3: Greedy meshing
	for (int face = 0; face < 6; ++face) {
		const int axis = face / 2;
		const int mesh_up_step = face % 2 == 0 ? 1 : 0;

		int merged_forward[CS_P2] = {0};
		for (int forward = 1; forward < CS_P - 1; ++forward) {
			uint64_t bits_walking_right = 0;
			int merged_right[CS_P] = {0};
			for (int right = 1; right < CS_P - 1; ++right) {
				const uint64_t bits_here = col_face_masks[right + (forward * CS_P) + (face * CS_P2)];
				const uint64_t bits_forward = forward >= CS ? 0 : col_face_masks[right + (forward * CS_P) + (face * CS_P2) + CS_P];
				const uint64_t bits_right = right >= CS ? 0 : col_face_masks[right + 1 + (forward * CS_P) + (face * CS_P2)];
				const uint64_t bits_merging_right = bits_here & bits_right;

				uint64_t bits_merging_forward = bits_here & bits_forward & ~bits_walking_right;
				uint64_t copy_front = bits_merging_forward;
				while (copy_front) {
					const int bit_pos = CTZ(copy_front);
					copy_front &= ~(1ULL << bit_pos);

					if (bit_pos == 0 || bit_pos == CS_P - 1) {
						continue;
					}

					if (compare_forward(voxels, axis, forward, right, bit_pos)) {
						++merged_forward[(right * CS_P) + bit_pos];
					} else {
						bits_merging_forward &= ~(1ULL << bit_pos);
					}
				}

				uint64_t bits_stopped_forward = bits_here & ~bits_merging_forward;
				while (bits_stopped_forward) {
					const int bit_pos = CTZ(bits_stopped_forward);
					bits_stopped_forward &= ~(1ULL << bit_pos);

					// Discards faces from neighbor voxels
					if (bit_pos == 0 || bit_pos == CS_P - 1) {
						continue;
					}

					if ((bits_merging_right & (1ULL << bit_pos)) != 0 &&
							merged_forward[(right * CS_P) + bit_pos] == merged_forward[(right + 1) * CS_P + bit_pos] &&
							compare_right(voxels, axis, forward, right, bit_pos)) {
						bits_walking_right |= 1ULL << bit_pos;
						++merged_right[bit_pos];
						merged_forward[(right * CS_P) + bit_pos] = 0;
						continue;
					}
					bits_walking_right &= ~(1ULL << bit_pos);

					const uint8_t mesh_left = right - merged_right[bit_pos];
					const uint8_t mesh_right = right + 1;
					const uint8_t mesh_front = forward - merged_forward[(right * CS_P) + bit_pos];
					const uint8_t mesh_back = forward + 1;
					const uint8_t mesh_up = bit_pos + mesh_up_step;

					const Voxel type = voxels[get_axis_i(axis, right, forward, bit_pos)];

					const int c = bit_pos;
					const uint8_t ao_F = solid_check(voxels[get_axis_i(axis, right, forward - 1, c)]) ? 1 : 0;
					const uint8_t ao_B = solid_check(voxels[get_axis_i(axis, right, forward + 1, c)]) ? 1 : 0;
					const uint8_t ao_L = solid_check(voxels[get_axis_i(axis, right - 1, forward, c)]) ? 1 : 0;
					const uint8_t ao_R = solid_check(voxels[get_axis_i(axis, right + 1, forward, c)]) ? 1 : 0;

					const uint8_t ao_LFC = solid_check(voxels[get_axis_i(axis, right - 1, forward - 1, c)]) ? 1 : 0;
					const uint8_t ao_LBC = solid_check(voxels[get_axis_i(axis, right - 1, forward + 1, c)]) ? 1 : 0;
					const uint8_t ao_RFC = solid_check(voxels[get_axis_i(axis, right + 1, forward - 1, c)]) ? 1 : 0;
					const uint8_t ao_RBC = solid_check(voxels[get_axis_i(axis, right + 1, forward + 1, c)]) ? 1 : 0;

					const uint8_t ao_LB = vertexAO(ao_L, ao_B, ao_LBC);
					const uint8_t ao_LF = vertexAO(ao_L, ao_F, ao_LFC);
					const uint8_t ao_RB = vertexAO(ao_R, ao_B, ao_RBC);
					const uint8_t ao_RF = vertexAO(ao_R, ao_F, ao_RFC);

					merged_forward[(right * CS_P) + bit_pos] = 0;
					merged_right[bit_pos] = 0;

					Vertex v1, v2, v3, v4;
					if (face == 0) {
						v1 = get_vertex(mesh_left, mesh_up, mesh_front, type, face, ao_LF);
						v2 = get_vertex(mesh_left, mesh_up, mesh_back, type, face, ao_LB);
						v3 = get_vertex(mesh_right, mesh_up, mesh_back, type, face, ao_RB);
						v4 = get_vertex(mesh_right, mesh_up, mesh_front, type, face, ao_RF);
					} else if (face == 1) {
						v1 = get_vertex(mesh_left, mesh_up, mesh_back, type, face, ao_LB);
						v2 = get_vertex(mesh_left, mesh_up, mesh_front, type, face, ao_LF);
						v3 = get_vertex(mesh_right, mesh_up, mesh_front, type, face, ao_RF);
						v4 = get_vertex(mesh_right, mesh_up, mesh_back, type, face, ao_RB);
					} else if (face == 2) {
						v1 = get_vertex(mesh_up, mesh_front, mesh_left, type, face, ao_LF);
						v2 = get_vertex(mesh_up, mesh_back, mesh_left, type, face, ao_LB);
						v3 = get_vertex(mesh_up, mesh_back, mesh_right, type, face, ao_RB);
						v4 = get_vertex(mesh_up, mesh_front, mesh_right, type, face, ao_RF);
					} else if (face == 3) {
						v1 = get_vertex(mesh_up, mesh_back, mesh_left, type, face, ao_LB);
						v2 = get_vertex(mesh_up, mesh_front, mesh_left, type, face, ao_LF);
						v3 = get_vertex(mesh_up, mesh_front, mesh_right, type, face, ao_RF);
						v4 = get_vertex(mesh_up, mesh_back, mesh_right, type, face, ao_RB);
					} else if (face == 4) {
						v1 = get_vertex(mesh_front, mesh_left, mesh_up, type, face, ao_LF);
						v2 = get_vertex(mesh_back, mesh_left, mesh_up, type, face, ao_LB);
						v3 = get_vertex(mesh_back, mesh_right, mesh_up, type, face, ao_RB);
						v4 = get_vertex(mesh_front, mesh_right, mesh_up, type, face, ao_RF);
					} else if (face == 5) {
						v1 = get_vertex(mesh_back, mesh_left, mesh_up, type, face, ao_LB);
						v2 = get_vertex(mesh_front, mesh_left, mesh_up, type, face, ao_LF);
						v3 = get_vertex(mesh_front, mesh_right, mesh_up, type, face, ao_RF);
						v4 = get_vertex(mesh_back, mesh_right, mesh_up, type, face, ao_RB);
					}

					insert_quad(vertices, v1, v2, v3, v4, ao_LB + ao_RF > ao_RB + ao_LF);
				}
			}
		}
	}
}
