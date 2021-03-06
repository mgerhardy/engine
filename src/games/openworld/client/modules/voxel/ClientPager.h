/**
 * @file
 */

#pragma once

#include "voxelworld/FilePersister.h"
#include "voxel/PagedVolume.h"
#include "http/HttpClient.h"
#include "core/SharedPtr.h"

namespace client {

class ClientPager : public voxel::PagedVolume::Pager {
private:
	http::HttpClient _httpClient;
	unsigned int _seed = 0u;
	int _mapId = -1;
	voxelworld::FilePersister _chunkPersister;
public:
	bool init(const core::String& baseUrl);

	bool pageIn(voxel::PagedVolume::PagerContext& ctx) override;
	void pageOut(voxel::PagedVolume::Chunk* chunk) override;
	void setSeed(unsigned int seed);
	void setMapId(int mapId);
};

typedef core::SharedPtr<ClientPager> ClientPagerPtr;

}
