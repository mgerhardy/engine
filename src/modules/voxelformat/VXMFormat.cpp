/**
 * @file
 */

#include "VXMFormat.h"
#include "core/Common.h"
#include "core/FourCC.h"
#include "core/Color.h"
#include "core/GLM.h"
#include "voxel/MaterialColor.h"
#include "core/Log.h"
#include <glm/common.hpp>

namespace voxel {

#define wrap(read) \
	if ((read) != 0) { \
		Log::error("Could not load vmx file: Not enough data in stream " CORE_STRINGIFY(read) " - still %i bytes left (line %i)", (int)stream.remaining(), (int)__LINE__); \
		return false; \
	}

#define wrapBool(read) \
	if ((read) != true) { \
		Log::error("Could not load vmx file: Not enough data in stream " CORE_STRINGIFY(read) " - still %i bytes left (line %i)", (int)stream.remaining(), (int)__LINE__); \
		return false; \
	}

bool VXMFormat::saveGroups(const VoxelVolumes& volumes, const io::FilePtr& file) {
	return false;
}

bool VXMFormat::loadGroups(const io::FilePtr& file, VoxelVolumes& volumes) {
	if (!(bool)file || !file->exists()) {
		Log::error("Could not load vmx file: File doesn't exist");
		return false;
	}
	io::FileStream stream(file.get());

	uint8_t magic[4];
	wrap(stream.readByte(magic[0]))
	wrap(stream.readByte(magic[1]))
	wrap(stream.readByte(magic[2]))
	wrap(stream.readByte(magic[3]))
	if (magic[0] != 'V' || magic[1] != 'X' || magic[2] != 'M') {
		Log::error("Could not load vxm file: Invalid magic found (%c%c%c%c)",
			magic[0], magic[1], magic[2], magic[3]);
		return false;
	}
	int version;
	if (magic[3] >= '0' && magic[3] <= '9') {
		version = magic[3] - '0';
	} else if (magic[3] >= 'A' && magic[3] <= 'C') {
		version = 10 + magic[3] - 'A';
	} else {
		Log::error("Invalid version found");
		return false;
	}

	if (version < 4 || version > 12) {
		Log::error("Could not load vxm file: Unsupported version found (%i)", version);
		return false;
	}

	bool foundPivot = false;
	glm::ivec3 ipivot { 0 };
	glm::uvec3 size;
	Log::debug("Found vxm%i", version);
	if (version >= 5) {
		if (version >= 6) {
			wrap(stream.readInt(size.x));
			wrap(stream.readInt(size.y));
			wrap(stream.readInt(size.z));
		}
		glm::vec3 pivot;
		wrap(stream.readFloat(pivot.x));
		wrap(stream.readFloat(pivot.y));
		wrap(stream.readFloat(pivot.z));
		ipivot.x = pivot.x;
		ipivot.y = pivot.y;
		ipivot.z = pivot.z;
		foundPivot = true;
		if (version >= 9) {
			uint8_t surface;
			wrap(stream.readByte(surface))
			if (surface) {
				uint32_t skipWidth = 0u;
				uint32_t skipHeight = 0u;
				uint32_t startx, starty, startz;
				uint32_t endx, endy, endz;
				uint32_t normal;
				// since version 10 the start and end values are floats
				// but for us this fact doesn't matter
				wrap(stream.readInt(startx))
				wrap(stream.readInt(starty))
				wrap(stream.readInt(startz))
				wrap(stream.readInt(endx))
				wrap(stream.readInt(endy))
				wrap(stream.readInt(endz))
				wrap(stream.readInt(normal))
				if (version >= 10) {
					wrap(stream.readInt(skipWidth))
					wrap(stream.readInt(skipHeight))
				} else {
					switch (normal) {
					case 0:
					case 1:
						skipWidth = endz - startz;
						skipHeight = endy - starty;
						break;
					case 2:
					case 3:
						skipWidth = endx - startx;
						skipHeight = endz - startz;
						break;
					case 4:
					case 5:
						skipWidth = endx - startx;
						skipHeight = endy - starty;
						break;
					}
				}
				stream.skip(skipWidth * skipHeight);
			}
		}
		if (version >= 8) {
			float dummy;                   // since version 'A'
			wrap(stream.readFloat(dummy)); // lod scale
			wrap(stream.readFloat(dummy)); // lod pivot x
			wrap(stream.readFloat(dummy)); // lod pivot y
			wrap(stream.readFloat(dummy)); // lod pivot z
		}
	}

	uint32_t lodLevels = 1;
	if (version >= 7) {
		wrap(stream.readInt(lodLevels));
	}
	for (uint32_t lodLevel = 0u; lodLevel < lodLevels; ++lodLevel) {
		glm::uvec2 textureDim;
		wrap(stream.readInt(textureDim.x));
		wrap(stream.readInt(textureDim.y));
		if (glm::any(glm::greaterThan(textureDim, glm::uvec2(2048)))) {
			Log::warn("Size of texture exceeds the max allowed value");
			return false;
		}

		if (version >= 11) {
			uint32_t size;
			wrap(stream.readInt(size));
			stream.skip(size);
		} else {
			uint32_t texAmount;
			wrap(stream.readInt(texAmount));
			if (texAmount > 0xFFFF) {
				Log::warn("Size of textures exceeds the max allowed value: %i", texAmount);
				return false;
			}

			Log::debug("texAmount: %i", (int)texAmount);
			for (uint32_t t = 0u; t < texAmount; t++) {
				char textureId[1024];
				wrapBool(stream.readString(sizeof(textureId), textureId, true))
				if (version >= 6) {
					uint32_t texZipped;
					wrap(stream.readInt(texZipped));
					stream.skip(texZipped);
				} else {
					Log::debug("tex: %i: %s", (int)t, textureId);
					uint32_t px = 0u;
					for (;;) {
						uint8_t rleStride;
						wrap(stream.readByte(rleStride));
						if (rleStride == 0u) {
							break;
						}

						struct TexColor {
							glm::u8vec3 rgb;
						};
						static_assert(sizeof(TexColor) == 3, "Unexpected TexColor size");
						stream.skip(sizeof(TexColor));
						px += rleStride;
						if (px > textureDim.x * textureDim.y * sizeof(TexColor)) {
							Log::error("RLE texture chunk exceeds max allowed size");
						}
					}
				}
			}
		}

		for (int i = 0; i < 6; ++i) {
			uint32_t quadAmount;
			wrap(stream.readInt(quadAmount));
			if (quadAmount > 0x40000U) {
				Log::warn("Size of quads exceeds the max allowed value");
				return false;
			}
			struct QuadVertex {
				glm::vec3 pos;
				glm::ivec2 uv;
			};
			static_assert(sizeof(QuadVertex) == 20, "Unexpected QuadVertex size");
			stream.skip(quadAmount * 4 * sizeof(QuadVertex));
		}
	}

	if (version <= 5) {
		wrap(stream.readInt(size.x));
		wrap(stream.readInt(size.y));
		wrap(stream.readInt(size.z));
	}

	if (glm::any(glm::greaterThan(size, glm::uvec3(MaxRegionSize)))) {
		Log::warn("Size of volume exceeds the max allowed value");
		return false;
	}
	if (glm::any(glm::lessThan(size, glm::uvec3(1)))) {
		Log::warn("Size of volume results in empty space");
		return false;
	}

	Log::debug("Volume of size %u:%u:%u", size.x, size.y, size.z);

	if (version >= 11) {
		stream.skip(1024);
		stream.skip(1024);
		uint8_t chunkAmount;
		wrap(stream.readByte(chunkAmount));
		for (int i = 0; i < (int) chunkAmount; ++i) {
			char chunkId[1024];
			wrapBool(stream.readString(sizeof(chunkId), chunkId, true))
			stream.skip(1);
			stream.skip(1); // chunk length
		}
	}

	uint8_t materialAmount;
	wrap(stream.readByte(materialAmount));
	Log::debug("Palette of size %i", (int)materialAmount);

	uint8_t *palette = new uint8_t[materialAmount];
	for (int i = 0; i < (int) materialAmount; ++i) {
		uint8_t blue;
		wrap(stream.readByte(blue));
		uint8_t green;
		wrap(stream.readByte(green));
		uint8_t red;
		wrap(stream.readByte(red));
		uint8_t alpha;
		wrap(stream.readByte(alpha));
		uint8_t emissive;
		wrap(stream.readByte(emissive));
		const glm::vec4& rgbaColor = core::Color::fromRGBA(red, green, blue, alpha);
		palette[i] = findClosestIndex(rgbaColor);
	}

	const Region region(glm::ivec3(0), glm::ivec3(size) - 1);

	if (!foundPivot) {
		ipivot = region.getCenter();
	}

	uint8_t maxLayers = 1;
	if (version > 12) {
		wrap(stream.readByte(maxLayers));
	}

	for (uint8_t layer = 0; layer < maxLayers; ++layer) {
		RawVolume* volume = new RawVolume(region);
		int idx = 0;
		for (;;) {
			uint8_t length;
			wrap(stream.readByte(length));
			if (length == 0u) {
				break;
			}

			uint8_t matIdx;
			wrap(stream.readByte(matIdx));
			if (matIdx == 0xFFU) {
				idx += length;
				continue;
			}
			if (matIdx >= materialAmount) {
				// at least try to load the rest
				idx += length;
				continue;
			}

			// left to right, bottom to top, front to back
			for (int i = idx; i < idx + length; i++) {
				const int xx = i / (int)(size.y * size.z);
				const int yy = (i / (int)size.z) % (int)size.y;
				const int zz = i % (int)size.z;
				const uint8_t index = palette[matIdx];
				voxel::VoxelType voxelType = voxel::VoxelType::Generic;
				if (index == 0) {
					voxelType = voxel::VoxelType::Air;
				}
				const Voxel voxel = createColorVoxel(voxelType, index);
				volume->setVoxel(size.x - 1 - xx, yy, zz, voxel);
			}
			idx += length;
		}
		const core::String& name = core::String::format("layer %i", (int)layer);
		volumes.push_back(VoxelVolume(volume, name, true, ipivot));
	}

	if (version >= 10) {
		uint8_t surface;
		wrap(stream.readByte(surface))
		if (surface) {
			uint32_t startx, starty, startz;
			uint32_t endx, endy, endz;
			uint32_t normal;
			wrap(stream.readInt(startx))
			wrap(stream.readInt(starty))
			wrap(stream.readInt(startz))
			wrap(stream.readInt(endx))
			wrap(stream.readInt(endy))
			wrap(stream.readInt(endz))
			wrap(stream.readInt(normal))
		}
	}

	delete[] palette;
	return true;
}

#undef wrap
#undef wrapBool

}
