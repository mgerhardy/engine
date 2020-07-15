/**
 * @file
 */

#include "SceneManager.h"

#include "voxelutil/VolumeMerger.h"
#include "voxelutil/VolumeCropper.h"
#include "voxelutil/VolumeRotator.h"
#include "voxelutil/VolumeMover.h"
#include "voxelutil/VolumeRescaler.h"
#include "voxelutil/VolumeVisitor.h"
#include "voxel/RawVolumeWrapper.h"
#include "voxel/RawVolumeMoveWrapper.h"
#include "voxelutil/Picking.h"
#include "voxel/Face.h"
#include "voxelgenerator/TreeGenerator.h"
#include "voxelworld/BiomeManager.h"
#include "voxelformat/Loader.h"
#include "voxelformat/VoxFormat.h"
#include "voxelformat/QBTFormat.h"
#include "voxelformat/CubFormat.h"
#include "voxelformat/QBFormat.h"
#include "voxelformat/VXMFormat.h"
#include "video/ScopedPolygonMode.h"
#include "video/ScopedLineWidth.h"
#include "video/ScopedBlendMode.h"
#include "video/Ray.h"
#include "math/Random.h"
#include "math/Axis.h"
#include "core/command/Command.h"
#include "core/command/CommandCompleter.h"
#include "core/ArrayLength.h"
#include "core/App.h"
#include "core/Log.h"
#include "core/Color.h"
#include "core/StringUtil.h"
#include "core/GLM.h"
#include "core/io/Filesystem.h"
#include "render/Gizmo.h"

#include "AxisUtil.h"
#include "CustomBindingContext.h"
#include "Config.h"
#include "tool/Clipboard.h"
#include "tool/Resize.h"
#include "tool/ImageUtils.h"
#include "anim/AnimationLuaSaver.h"
#include "core/TimeProvider.h"
#include "attrib/ShadowAttributes.h"

#include <limits>
#include <iterator>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/transform.hpp>

namespace voxedit {

SceneManager::~SceneManager() {
	core_assert_msg(_initialized == 0, "SceneManager was not properly shut down");
}

voxel::Region SceneManager::region() const {
	return _volumeRenderer.region();
}

bool SceneManager::loadPalette(const core::String& paletteName) {
	const io::FilesystemPtr& filesystem = io::filesystem();
	const io::FilePtr& paletteFile = filesystem->open(core::string::format("palette-%s.png", paletteName.c_str()));
	const io::FilePtr& luaFile = filesystem->open(core::string::format("palette-%s.lua", paletteName.c_str()));
	if (voxel::overrideMaterialColors(paletteFile, luaFile)) {
		core::Var::getSafe(cfg::VoxEditLastPalette)->setVal(paletteName);
		return true;
	}
	return false;
}

bool SceneManager::importPalette(const core::String& file) {
	const image::ImagePtr& img = image::loadImage(file, false);
	if (!img->isLoaded()) {
		return false;
	}
	uint32_t buf[256];
	if (!voxel::createPalette(img, buf, lengthof(buf))) {
		return false;
	}
	const core::String luaString = "";
	if (!voxel::overrideMaterialColors((const uint8_t*)buf, sizeof(buf), luaString)) {
		Log::warn("Failed to import palette for image %s", file.c_str());
		return false;
	}
	const io::FilesystemPtr& fs = io::filesystem();
	const core::String paletteName(core::string::extractFilename(file.c_str()));
	const core::String& paletteFilename = core::string::format("palette-%s.png", paletteName.c_str());
	const io::FilePtr& pngFile = fs->open(paletteFilename, io::FileMode::Write);
	if (image::Image::writePng(pngFile->name().c_str(), (const uint8_t*)buf, lengthof(buf), 1, 4)) {
		fs->write(core::string::format("palette-%s.lua", paletteName.c_str()), luaString);
		core::Var::getSafe(cfg::VoxEditLastPalette)->setVal(paletteName);
	} else {
		Log::warn("Failed to write image");
	}
	return true;
}

bool SceneManager::importAsPlane(const core::String& file) {
	const image::ImagePtr& img = image::loadImage(file, false);
	if (!img->isLoaded()) {
		return false;
	}
	voxel::RawVolume* v = voxedit::importAsPlane(img);
	if (v == nullptr) {
		return false;
	}
	const core::String filename = core::string::extractFilename(img->name().c_str());
	if (!_layerMgr.addLayer(filename.c_str(), true, v, glm::zero<glm::ivec3>())) {
		delete v;
		return false;
	}
	return true;
}

bool SceneManager::importHeightmap(const core::String& file) {
	const int layerId = _layerMgr.activeLayer();
	voxel::RawVolume* v = volume(layerId);
	if (v == nullptr) {
		return false;
	}
	const image::ImagePtr& img = image::loadImage(file, false);
	if (!img->isLoaded()) {
		return false;
	}
	voxel::RawVolumeWrapper wrapper(v);
	voxedit::importHeightmap(wrapper, img);
	modified(layerId, wrapper.dirtyRegion());
	return true;
}

void SceneManager::autosave() {
	if (!_needAutoSave) {
		return;
	}
	const core::TimeProviderPtr& timeProvider = core::App::getInstance()->timeProvider();
	const uint64_t delay = _autoSaveSecondsDelay->intVal();
	if (_lastAutoSave + delay > timeProvider->tickSeconds()) {
		return;
	}
	core::String autoSaveFilename;
	if (_lastFilename.empty()) {
		autoSaveFilename = "autosave-noname.vox";
	} else {
		if (core::string::startsWith(_lastFilename.c_str(), "autosave-")) {
			autoSaveFilename = _lastFilename;
		} else {
			const io::FilePtr file = io::filesystem()->open(_lastFilename);
			const core::String& p = file->path();
			const core::String& f = file->fileName();
			const core::String& e = file->extension();
			autoSaveFilename = core::string::format("%s/autosave-%s.%s",
					p.c_str(), f.c_str(), e.c_str());
		}
	}
	if (save(autoSaveFilename, true)) {
		Log::info("Autosave file %s", autoSaveFilename.c_str());
	} else {
		Log::warn("Failed to autosave");
	}
	_lastAutoSave = timeProvider->tickSeconds();
}

bool SceneManager::saveLayer(int layerId, const core::String& file) {
	voxel::RawVolume* v = _volumeRenderer.volume(layerId);
	if (v == nullptr) {
		return true;
	}
	const io::FilePtr& filePtr = io::filesystem()->open(file, io::FileMode::SysWrite);
	if (!filePtr->validHandle()) {
		Log::warn("Failed to open the given file '%s' for writing", file.c_str());
		return false;
	}
	const Layer& layer = _layerMgr.layer(layerId);
	voxel::VoxelVolumes volumes;
	volumes.push_back(voxel::VoxelVolume(v, layer.name, layer.visible));
	if (voxelformat::saveVolumeFormat(filePtr, volumes)) {
		Log::info("Saved layer %i to %s", layerId, filePtr->name().c_str());
		return true;
	}
	Log::warn("Failed to save layer %i to %s", layerId, filePtr->name().c_str());
	return false;
}

bool SceneManager::saveLayers(const core::String& dir) {
	const int layers = (int)_layerMgr.layers().size();
	for (int idx = 0; idx < layers; ++idx) {
		voxel::RawVolume* v = _volumeRenderer.volume(idx);
		if (v == nullptr) {
			return true;
		}
		const Layer& layer = _layerMgr.layer(idx);
		saveLayer(idx, dir + "/" + layer.name + ".vox");
	}
	return true;
}

bool SceneManager::save(const core::String& file, bool autosave) {
	voxel::VoxelVolumes volumes;
	const int layers = (int)_layerMgr.layers().size();
	Log::debug("Trying to save %i layers", layers);
	for (int idx = 0; idx < layers; ++idx) {
		voxel::RawVolume* v = _volumeRenderer.volume(idx);
		if (v == nullptr) {
			Log::debug("No volume for layer %i", idx);
			continue;
		}
		if (_volumeRenderer.empty(idx)) {
			Log::debug("Layer %i is empty", idx);
			continue;
		}
		const Layer& layer = _layerMgr.layer(idx);
		volumes.push_back(voxel::VoxelVolume(v, layer.name, layer.visible));
	}

	if (volumes.empty()) {
		Log::warn("No volumes for saving found");
		return false;
	}

	if (file.empty()) {
		Log::warn("No filename given for saving");
		return false;
	}
	const io::FilePtr& filePtr = io::filesystem()->open(file, io::FileMode::SysWrite);
	if (!filePtr->validHandle()) {
		Log::warn("Failed to open the given file '%s' for writing", file.c_str());
		return false;
	}
	core::String ext = filePtr->extension();
	if (ext.empty()) {
		Log::warn("No file extension given for saving, assuming vox");
		ext = "vox";
	}

	bool saved = voxelformat::saveVolumeFormat(filePtr, volumes);
	if (!saved) {
		Log::warn("Failed to save %s file - retry as qb instead", ext.c_str());
		voxel::QBFormat f;
		saved = f.saveGroups(volumes, filePtr);
	}
	if (saved) {
		if (!autosave) {
			_dirty = false;
			_lastFilename = file;
		}
		core::Var::get(cfg::VoxEditLastFile)->setVal(file);
		_needAutoSave = false;
	} else {
		Log::warn("Failed to save to desired format");
	}
	return saved;
}

bool SceneManager::prefab(const core::String& file) {
	if (file.empty()) {
		return false;
	}
	const io::FilePtr& filePtr = io::filesystem()->open(file);
	if (!(bool)filePtr) {
		Log::error("Failed to open model file %s", file.c_str());
		return false;
	}
	voxel::VoxelVolumes newVolumes;
	if (!voxelformat::loadVolumeFormat(filePtr, newVolumes)) {
		return false;
	}
	for (const auto& v : newVolumes) {
		_layerMgr.addLayer(v.name.c_str(), v.visible, v.volume, v.pivot);
	}
	return true;
}

bool SceneManager::load(const core::String& file) {
	if (file.empty()) {
		return false;
	}
	const io::FilePtr& filePtr = io::filesystem()->open(file);
	if (!(bool)filePtr) {
		Log::error("Failed to open model file '%s'", file.c_str());
		return false;
	}
	voxel::VoxelVolumes newVolumes;
	if (!voxelformat::loadVolumeFormat(filePtr, newVolumes)) {
		return false;
	}
	const core::String& ext = filePtr->extension();
	_lastFilename = filePtr->fileName() + "." + ext;
	if (!setNewVolumes(newVolumes)) {
		return false;
	}
	_needAutoSave = false;
	_dirty = false;
	return true;
}

void SceneManager::setMousePos(int x, int y) {
	if (_mouseCursor.x == x && _mouseCursor.y == y) {
		return;
	}
	_mouseCursor.x = x;
	_mouseCursor.y = y;
	_traceViaMouse = true;
}

void SceneManager::handleAnimationViewUpdate(int layerId) {
	if (!_animationUpdate && _animationLayerDirtyState == -1) {
		// the first layer
		_animationLayerDirtyState = layerId;
	} else if (_animationUpdate) {
		// a second layer was modified (maybe a group action)
		_animationLayerDirtyState = -1;
	}
	_animationUpdate = true;
}

void SceneManager::queueRegionExtraction(int layerId, const voxel::Region& region) {
	bool addNew = true;
	for (const auto& r : _extractRegions) {
		if (r.layer != layerId) {
			continue;
		}
		if (r.region.containsRegion(region)) {
			addNew = false;
			break;
		}
	}
	if (addNew) {
		_extractRegions.push_back({region, layerId});
	}
}

void SceneManager::modified(int layerId, const voxel::Region& modifiedRegion, bool markUndo) {
	Log::debug("Modified layer %i, undo state: %s", layerId, markUndo ? "true" : "false");
	voxel::logRegion("Modified", modifiedRegion);
	if (markUndo) {
		_mementoHandler.markUndo(layerId, _layerMgr.layer(layerId).name, _volumeRenderer.volume(layerId), MementoType::Modification, modifiedRegion);
	}
	if (modifiedRegion.isValid()) {
		queueRegionExtraction(layerId, modifiedRegion);
	}
	_dirty = true;
	_needAutoSave = true;
	handleAnimationViewUpdate(layerId);
	resetLastTrace();
}

void SceneManager::thicken(int amount) {
	const glm::ivec3 dimensions(amount + 1);
	_layerMgr.foreachGroupLayer([&] (int layerId) {
		voxel::RawVolume* v = volume(layerId);
		if (v == nullptr) {
			return;
		}
		voxel::RawVolume* thinkened = new voxel::RawVolume(v->region());
		voxel::RawVolumeWrapper wrapper(thinkened);
		voxelutil::visitVolume(*v, [&] (int32_t x, int32_t y, int32_t z, voxel::Voxel voxel) {
			voxelgenerator::shape::createCube(wrapper, glm::ivec3(x, y, z), dimensions, voxel);
		});
		setNewVolume(layerId, thinkened, true);
		modified(layerId, wrapper.dirtyRegion());
	});
}

void SceneManager::colorToNewLayer(const voxel::Voxel voxelColor) {
	voxel::RawVolume* newVolume = new voxel::RawVolume(region());
	_layerMgr.foreachGroupLayer([&] (int layerId) {
		voxel::RawVolumeWrapper wrapper(volume(layerId));
		voxelutil::visitVolume(wrapper, [&] (int32_t x, int32_t y, int32_t z, const voxel::Voxel& voxel) {
			if (voxel.getColor() == voxelColor.getColor()) {
				newVolume->setVoxel(x, y, z, voxel);
				wrapper.setVoxel(x, y, z, voxel::Voxel());
			}
		});
		modified(layerId, wrapper.dirtyRegion());
	});
	const core::String& name = core::string::format("color: %i", (int)voxelColor.getColor());
	_layerMgr.addLayer(name.c_str(), true, newVolume);
}

void SceneManager::scale(int layerId) {
	voxel::RawVolume* srcVolume = volume(layerId);
	if (srcVolume == nullptr) {
		return;
	}
	const voxel::Region srcRegion = srcVolume->region();
	const glm::ivec3& targetDimensionsHalf = (srcRegion.getDimensionsInVoxels() / 2) - 1;
	if (targetDimensionsHalf.x < 0 || targetDimensionsHalf.y < 0 || targetDimensionsHalf.z < 0) {
		Log::debug("Can't scale anymore");
		return;
	}
	const voxel::Region destRegion(srcRegion.getLowerCorner(), srcRegion.getLowerCorner() + targetDimensionsHalf);
	voxel::RawVolume* destVolume = new voxel::RawVolume(destRegion);
	rescaleVolume(*srcVolume, *destVolume);
	setNewVolume(layerId, destVolume, true);
	modified(layerId, srcRegion);
}

void SceneManager::crop() {
	const int layerId = _layerMgr.activeLayer();
	if (_volumeRenderer.empty(layerId)) {
		Log::info("Empty volumes can't be cropped");
		return;
	}
	voxel::RawVolume* newVolume = voxel::cropVolume(volume(layerId));
	if (newVolume == nullptr) {
		return;
	}
	setNewVolume(layerId, newVolume, true);
	modified(layerId, newVolume->region());
}

void SceneManager::resize(const glm::ivec3& size) {
	const glm::ivec3 refPos = referencePosition();
	_layerMgr.foreachGroupLayer([&] (int layerId) {
		voxel::RawVolume* newVolume = voxedit::tool::resize(volume(layerId), size);
		if (newVolume == nullptr) {
			return;
		}
		setNewVolume(layerId, newVolume, false);
		if (glm::all(glm::greaterThanEqual(size, glm::zero<glm::ivec3>()))) {
			// we don't have to reextract a mesh if only new empty voxels were added.
			modified(layerId, voxel::Region::InvalidRegion);
		} else {
			modified(layerId, newVolume->region());
		}
	});
	if (region().containsPoint(refPos)) {
		setReferencePosition(refPos);
	}
}

voxel::RawVolume* SceneManager::volume(int idx) {
	voxel::RawVolume* v = _volumeRenderer.volume(idx);
	//core_assert_msg(v != nullptr, "Volume for index %i is null", idx);
	return v;
}

voxel::RawVolume* SceneManager::modelVolume() {
	const int idx = _layerMgr.activeLayer();
	return volume(idx);
}

void SceneManager::undo() {
	const MementoState& s = _mementoHandler.undo();
	ScopedMementoHandlerLock lock(_mementoHandler);
	if (s.type == MementoType::LayerRenamed) {
		_layerMgr.rename(s.layer, s.name);
		return;
	}
	voxel::RawVolume* v = MementoData::toVolume(s.data);
	if (v == nullptr) {
		_layerMgr.deleteLayer(s.layer, false);
		return;
	}
	Log::debug("Volume found in undo state for layer: %i with name %s", s.layer, s.name.c_str());
	_layerMgr.activateLayer(s.layer, s.name.c_str(), true, v, s.region, referencePosition());
}

void SceneManager::copy() {
	const Selection& selection = _modifier.selection();
	if (!selection.isValid()) {
		return;
	}
	const int idx = _layerMgr.activeLayer();
	voxel::RawVolume* model = volume(idx);
	if (_copy != nullptr) {
		delete _copy;
	}
	_copy = voxedit::tool::copy(model, selection);
}

void SceneManager::paste(const glm::ivec3& pos) {
	if (_copy == nullptr) {
		Log::debug("Nothing copied yet - failed to paste");
		return;
	}
	const int idx = _layerMgr.activeLayer();
	voxel::RawVolume* model = volume(idx);
	voxel::Region modifiedRegion;
	voxedit::tool::paste(model, _copy, pos, modifiedRegion);
	if (!modifiedRegion.isValid()) {
		Log::debug("Failed to paste");
		return;
	}
	modified(idx, modifiedRegion);
}

void SceneManager::cut() {
	const Selection& selection = _modifier.selection();
	if (!selection.isValid()) {
		Log::debug("Nothing selected - failed to cut");
		return;
	}
	const int idx = _layerMgr.activeLayer();
	voxel::RawVolume* model = volume(idx);
	if (_copy != nullptr) {
		delete _copy;
	}
	voxel::Region modifiedRegion;
	_copy = voxedit::tool::cut(model, selection, modifiedRegion);
	if (_copy == nullptr) {
		Log::debug("Failed to cut");
		return;
	}
	modified(idx, modifiedRegion);
}

void SceneManager::redo() {
	const MementoState& s = _mementoHandler.redo();
	ScopedMementoHandlerLock lock(_mementoHandler);
	if (s.type == MementoType::LayerRenamed) {
		_layerMgr.rename(s.layer, s.name);
		return;
	}
	voxel::RawVolume* v = MementoData::toVolume(s.data);
	if (v == nullptr) {
		_layerMgr.deleteLayer(s.layer, false);
		return;
	}
	Log::debug("Volume found in redo state for layer: %i with name %s", s.layer, s.name.c_str());
	_layerMgr.activateLayer(s.layer, s.name.c_str(), true, v, s.region, referencePosition());
}

void SceneManager::resetLastTrace() {
	if (!_traceViaMouse) {
		return;
	}
	_lastRaytraceX = _lastRaytraceY = -1;
}

bool SceneManager::merge(int layerId1, int layerId2) {
	std::vector<const voxel::RawVolume*> volumes;
	volumes.resize(2);
	volumes[0] = _volumeRenderer.volume(layerId1);
	if (volumes[0] == nullptr) {
		return false;
	}
	volumes[1] = _volumeRenderer.volume(layerId2);
	if (volumes[1] == nullptr) {
		return false;
	}
	voxel::RawVolume* volume = voxel::merge(volumes);
	if (!setNewVolume(layerId1, volume, true)) {
		delete volume;
		return false;
	}
	// TODO: the memento states are not yet perfect
	modified(layerId1, volume->region(), true);
	_layerMgr.deleteLayer(layerId2);
	return true;
}

void SceneManager::resetSceneState() {
	_animationLayerDirtyState = -1;
	_animationIdx = 0;
	_animationUpdate = false;
	_editMode = EditMode::Model;
	_mementoHandler.clearStates();
	const int layerId = _layerMgr.activeLayer();
	// push the initial state of the current layer to the memento handler to
	// be able to undo your next step
	Log::debug("New volume for layer %i", layerId);
	_mementoHandler.markUndo(layerId, _layerMgr.layer(layerId).name, _volumeRenderer.volume(layerId));
	_dirty = false;
	_result = voxel::PickResult();
	setCursorPosition(cursorPosition(), true);
	resetLastTrace();
}

bool SceneManager::setNewVolumes(const voxel::VoxelVolumes& volumes) {
	core_trace_scoped(SetNewVolumes);
	const int volumeCnt = (int)volumes.size();
	if (volumeCnt == 0) {
		const voxel::Region region(glm::ivec3(0), glm::ivec3(size() - 1));
		return newScene(true, "", region);
	}
	const int maxLayers = _layerMgr.maxLayers();
	if (volumeCnt > maxLayers) {
		Log::warn("Max supported layer size exceeded: %i (max supported: %i)",
				volumeCnt, maxLayers);
	}
	_volumeRenderer.clearPendingExtractions();
	for (int idx = 0; idx < maxLayers; ++idx) {
		_layerMgr.deleteLayer(idx, true);
	}
	int valid = 0;
	for (int idx = 0; idx < volumeCnt; ++idx) {
		const int layerId = _layerMgr.addLayer(volumes[idx].name.c_str(), volumes[idx].visible, volumes[idx].volume, volumes[idx].pivot);
		if (layerId >= 0) {
			++valid;
		}
	}
	if (valid == 0) {
		const voxel::Region region(glm::ivec3(0), glm::ivec3(size() - 1));
		return newScene(true, "", region);
	}
	_layerMgr.findNewActiveLayer();
	resetSceneState();
	return true;
}

static inline math::AABB<float> aabb(const voxel::Region& region) {
	const math::AABB<int> intaabb(region.getLowerCorner(), region.getUpperCorner() + 1);
	return math::AABB<float>(glm::vec3(intaabb.getLowerCorner()), glm::vec3(intaabb.getUpperCorner()));
}

void SceneManager::updateGridRenderer(const voxel::Region& region) {
	_gridRenderer.update(aabb(region));
}

bool SceneManager::setNewVolume(int idx, voxel::RawVolume* volume, bool deleteMesh) {
	core_trace_scoped(SetNewVolume);
	if (idx < 0 || idx >= _layerMgr.maxLayers()) {
		return false;
	}
	const voxel::Region& region = volume->region();
	delete _volumeRenderer.setVolume(idx, volume, deleteMesh);

	updateAABBMesh();
	if (volume != nullptr) {
		updateGridRenderer(region);
	} else {
		_gridRenderer.clear();
	}

	_dirty = false;
	_result = voxel::PickResult();
	setCursorPosition(cursorPosition(), true);
	glm::ivec3 center = region.getCenter();
	center.y = region.getLowerY();
	setReferencePosition(center);
	resetLastTrace();
	return true;
}

bool SceneManager::newScene(bool force, const core::String& name, const voxel::Region& region) {
	if (dirty() && !force) {
		return false;
	}
	_volumeRenderer.clearPendingExtractions();
	const int layers = _layerMgr.maxLayers();
	for (int idx = 0; idx < layers; ++idx) {
		_layerMgr.deleteLayer(idx, true);
	}
	core_assert_always(_layerMgr.validLayers() == 0);
	core_assert_always(_layerMgr.addLayer(name.c_str(), true, new voxel::RawVolume(region)) != -1);
	core_assert_always(_layerMgr.validLayers() == 1);
	glm::ivec3 center = region.getCenter();
	center.y = region.getLowerY();
	setReferencePosition(center);
	_layerMgr.findNewActiveLayer();
	resetSceneState();
	return true;
}

void SceneManager::rotate(int layerId, const glm::ivec3& angle, bool increaseSize, bool rotateAroundReferencePosition) {
	const voxel::RawVolume* model = volume(layerId);
	if (model == nullptr) {
		return;
	}
	voxel::RawVolume* newVolume;
	const bool axisRotation = !rotateAroundReferencePosition && !increaseSize;
	if (axisRotation && angle == glm::ivec3(90, 0, 0)) {
		newVolume = voxel::rotateAxis(model, math::Axis::X);
	} else if (axisRotation && angle == glm::ivec3(0, 90, 0)) {
		newVolume = voxel::rotateAxis(model, math::Axis::Y);
	} else if (axisRotation && angle == glm::ivec3(0, 0, 90)) {
		newVolume = voxel::rotateAxis(model, math::Axis::Z);
	} else {
		const glm::vec3 pivot = rotateAroundReferencePosition ? glm::vec3(referencePosition()) : model->region().getCenterf();
		newVolume = voxel::rotateVolume(model, angle, voxel::Voxel(), pivot, increaseSize);
	}
	voxel::Region r = newVolume->region();
	r.accumulate(model->region());
	setNewVolume(layerId, newVolume);
	modified(layerId, r);
}

void SceneManager::rotate(int angleX, int angleY, int angleZ, bool increaseSize, bool rotateAroundReferencePosition) {
	const glm::ivec3 angle(angleX, angleY, angleZ);
	_layerMgr.foreachGroupLayer([&] (int layerId) {
		rotate(layerId, angle, increaseSize, rotateAroundReferencePosition);
	});
}

void SceneManager::move(int layerId, const glm::ivec3& m) {
	const voxel::RawVolume* model = volume(layerId);
	voxel::RawVolume* newVolume = new voxel::RawVolume(model->region());
	voxel::RawVolumeMoveWrapper wrapper(newVolume);
	voxel::moveVolume(&wrapper, model, m);
	setNewVolume(layerId, newVolume);
	modified(layerId, newVolume->region());
}

void SceneManager::move(int x, int y, int z) {
	const glm::ivec3 v(x, y, z);
	_layerMgr.foreachGroupLayer([&] (int layerId) {
		move(layerId, v);
	});
}

void SceneManager::shift(int layerId, const glm::ivec3& m) {
	voxel::RawVolume* model = volume(layerId);
	Log::debug("Shift region by %s on layer %i", glm::to_string(m).c_str(), layerId);
	voxel::Region oldRegion = model->region();
	_referencePos += m;
	_modifier.translate(m);
	_volumeRenderer.translate(layerId, m);
	setGizmoPosition();
	const voxel::Region& newRegion = model->region();
	updateGridRenderer(newRegion);
	updateAABBMesh();
	oldRegion.accumulate(newRegion);
	modified(layerId, oldRegion);
}

void SceneManager::shift(int x, int y, int z) {
	const glm::ivec3 v(x, y, z);
	_layerMgr.foreachGroupLayer([&] (int layerId) {
		shift(layerId, v);
	});
}

void SceneManager::executeGizmoAction(const glm::ivec3& delta, render::GizmoMode mode) {
	// TODO: memento state at pressing and releasing
	if (mode == render::GizmoMode::TranslateX) {
		if (delta.x != 0) {
			shift(delta.x, 0, 0);
		}
	} else if (mode == render::GizmoMode::TranslateY) {
		if (delta.y != 0) {
			shift(0, delta.y, 0);
		}
	} else if (mode == render::GizmoMode::TranslateZ) {
		if (delta.z != 0) {
			shift(0, 0, delta.z);
		}
	}
}

bool SceneManager::setGridResolution(int resolution) {
	const bool ret = gridRenderer().setGridResolution(resolution);
	if (!ret) {
		return false;
	}

	const int res = gridRenderer().gridResolution();
	_modifier.setGridResolution(res);
	setCursorPosition(cursorPosition(), true);

	return true;
}

void SceneManager::renderAnimation(const video::Camera& camera) {
	attrib::ShadowAttributes attrib;
	const double deltaFrameSeconds = core::App::getInstance()->deltaFrameSeconds();
	if (_animationUpdate) {
		const voxedit::Layers& layers = _layerMgr.layers();
		const size_t layerAmount = layers.size();
		for (size_t i = 0u; i < layerAmount; ++i) {
			const voxel::RawVolume* v = volume(i);
			if (v == nullptr) {
				continue;
			}
			if (_animationLayerDirtyState >= 0 && _animationLayerDirtyState != (int)i) {
				Log::debug("Don't update layer %i", (int)i);
				continue;
			}
			const voxedit::Layer& l = layers[i];
			const core::String& value = l.metadataById("type");
			if (value.empty()) {
				Log::debug("No type metadata found on layer %i", (int)i);
				continue;
			}
			const int characterMeshTypeId = core::string::toInt(value);
			const animation::AnimationSettings& animSettings = animationEntity().animationSettings();
			const core::String& path = animSettings.paths[characterMeshTypeId];
			if (path.empty()) {
				Log::debug("No path found for layer %i", (int)i);
				continue;
			}
			voxel::Mesh mesh;
			_volumeRenderer.toMesh(i, &mesh);
			const core::String& fullPath = animSettings.fullPath(characterMeshTypeId);
			_animationCache->putMesh(fullPath.c_str(), mesh);
			Log::debug("Updated mesh on layer %i for path %s", (int)i, fullPath.c_str());
		}
		if (!animationEntity().initMesh(_animationCache)) {
			Log::warn("Failed to update the mesh");
		}
		_animationUpdate = false;
		_animationLayerDirtyState = -1;
	}
	animationEntity().update(deltaFrameSeconds, attrib);
	_animationRenderer.render(animationEntity(), camera);
}

void SceneManager::updateAABBMesh() {
	Log::debug("Update aabb mesh");
	_shapeBuilder.clear();
	_shapeBuilder.setColor(core::Color::Gray);
	for (int idx = 0; idx < (int)_layerMgr.layers().size(); ++idx) {
		const Layer& layer = _layerMgr.layer(idx);
		if (!layer.valid) {
			continue;
		}
		const voxel::RawVolume* volume = _volumeRenderer.volume(idx);
		core_assert_always(volume != nullptr);
		const voxel::Region& region = volume->region();
		_shapeBuilder.aabb(aabb(region));
	}
	const voxel::Region& region = modelVolume()->region();
	_shapeBuilder.setColor(core::Color::White);
	_shapeBuilder.aabb(aabb(region));
	_shapeRenderer.createOrUpdate(_aabbMeshIndex, _shapeBuilder);
}

void SceneManager::render(const video::Camera& camera, uint8_t renderMask) {
	const bool depthTest = video::enable(video::State::DepthTest);
	const bool renderUI = (renderMask & RenderUI) != 0u;
	const bool renderScene = (renderMask & RenderScene) != 0u;
	if (renderUI) {
		if (_editMode == EditMode::Scene) {
			_shapeRenderer.render(_aabbMeshIndex, camera);
		} else {
			const voxel::Region& region = modelVolume()->region();
			_gridRenderer.render(camera, aabb(region));
		}
	}
	if (renderScene) {
		_volumeRenderer.render(camera, _renderShadow);
	}
	if (renderUI) {
		if (_editMode == EditMode::Scene) {
			_gizmo.render(camera);
		} else {
			_modifier.render(camera);

			// TODO: render error if rendered last - but be before grid renderer to get transparency.
			if (_renderLockAxis) {
				for (int i = 0; i < lengthof(_planeMeshIndex); ++i) {
					_shapeRenderer.render(_planeMeshIndex[i], camera);
				}
			}
			if (_renderAxis) {
				_gizmo.render(camera);
			}
		}
		if (!depthTest) {
			video::disable(video::State::DepthTest);
		}
		_shapeRenderer.render(_referencePointMesh, camera, _referencePointModelMatrix);
	} else if (!depthTest) {
		video::disable(video::State::DepthTest);
	}
}

void SceneManager::construct() {
	_layerMgr.construct();
	_modifier.construct();
	_mementoHandler.construct();
	_volumeRenderer.construct();

	core::Var::get(cfg::VoxEditLastPalette, "nippon");
	_modelSpace = core::Var::get(cfg::VoxEditModelSpace, "1");

	for (int i = 0; i < lengthof(DIRECTIONS); ++i) {
		core::Command::registerActionButton(
				core::string::format("movecursor%s", DIRECTIONS[i].postfix),
				_move[i]).setBindingContext(BindingContext::Model);
	}

	core::Command::registerActionButton("zoom_in", _zoomIn).setBindingContext(BindingContext::Editing);
	core::Command::registerActionButton("zoom_out", _zoomOut).setBindingContext(BindingContext::Editing);

	core::Command::registerCommand("animation_cycle", [this] (const core::CmdArgs& argv) {
		int offset = 1;
		if (argv.size() > 0) {
			offset = core::string::toInt(argv[0]);
		}
		_animationIdx += offset;
		while (_animationIdx < 0) {
			_animationIdx += (core::enumVal(animation::Animation::MAX) + 1);
		}
		_animationIdx %= (core::enumVal(animation::Animation::MAX) + 1);
		Log::info("current animation idx: %i", _animationIdx);
		animationEntity().setAnimation((animation::Animation)_animationIdx, true);
	});

	core::Command::registerCommand("animation_save", [&] (const core::CmdArgs& args) {
		core::String name = "entity";
		if (!args.empty()) {
			name = args[0];
		}
		saveAnimationEntity(name.c_str());
	});

	core::Command::registerCommand("togglescene", [this] (const core::CmdArgs& args) {
		toggleEditMode();
	}).setHelp("Toggle scene mode on/off");

	core::Command::registerCommand("layerssave", [&] (const core::CmdArgs& args) {
		core::String dir = ".";
		if (!args.empty()) {
			dir = args[0];
		}
		if (!saveLayers(dir)) {
			Log::error("Failed to save layers to dir: %s", dir.c_str());
		}
	}).setHelp("Save all layers into filenames represented by their layer names");

	core::Command::registerCommand("layersave", [&] (const core::CmdArgs& args) {
		const int argc = args.size();
		if (argc < 1) {
			Log::info("Usage: layersave <layerId> [<file>]");
			return;
		}
		const int layerId = core::string::toInt(args[0]);
		core::String file = core::string::format("layer%i.vox", layerId);
		if (args.size() == 2) {
			file = args[1];
		}
		if (!saveLayer(layerId, file)) {
			Log::error("Failed to save layer %i to file: %s", layerId, file.c_str());
		}
	}).setHelp("Save a single layer to the given path with their layer names");

	core::Command::registerCommand("newscene", [&] (const core::CmdArgs& args) {
		const char *name = args.size() > 0 ? args[0].c_str() : "";
		const char *width = args.size() > 1 ? args[1].c_str() : "64";
		const char *height = args.size() > 2 ? args[2].c_str() : width;
		const char *depth = args.size() > 3 ? args[3].c_str() : height;
		const int iw = core::string::toInt(width) - 1;
		const int ih = core::string::toInt(height) - 1;
		const int id = core::string::toInt(depth) - 1;
		const voxel::Region region(glm::zero<glm::ivec3>(), glm::ivec3(iw, ih, id));
		if (!region.isValid()) {
			Log::warn("Invalid size provided (%i:%i:%i)", iw, ih, id);
			return;
		}
		if (!newScene(true, name, region)) {
			Log::warn("Could not create new scene");
		}
	}).setHelp("Create a new scene (with a given name and width, height, depth - all optional)");

	core::Command::registerCommand("noise", [&] (const core::CmdArgs& args) {
		const int argc = args.size();
		if (argc != 4) {
			Log::info("Usage: noise <octaves> <lacunarity> <frequency> <gain>");
			return;
		}
		int octaves = core::string::toInt(args[0]);
		float lacunarity = core::string::toFloat(args[0]);
		float frequency = core::string::toFloat(args[0]);
		float gain = core::string::toFloat(args[0]);
		voxelgenerator::noise::NoiseType type = voxelgenerator::noise::NoiseType::ridgedMF;
		noise(octaves, lacunarity, frequency, gain, type);
	}).setHelp("Fill the volume with noise");

	core::Command::registerCommand("crop", [&] (const core::CmdArgs& args) {
		crop();
	}).setHelp("Crop the volume");

	core::Command::registerCommand("scale", [&] (const core::CmdArgs& args) {
		const int argc = args.size();
		int layerId = _layerMgr.activeLayer();
		if (argc == 1) {
			layerId = core::string::toInt(args[0]);
		} else {
			Log::info("Scale layer %i by half", layerId);
			Log::info("Usage: scale (<layerId>)");
		}
		scale(layerId);
	}).setHelp("Scale the volume");

	core::Command::registerCommand("colortolayer", [&] (const core::CmdArgs& args) {
		const int argc = args.size();
		if (argc < 1) {
			const voxel::Voxel voxel = _modifier.cursorVoxel();
			colorToNewLayer(voxel);
		} else {
			const uint8_t index = core::string::toInt(args[0]);
			const voxel::Voxel voxel = voxel::createVoxel(voxel::VoxelType::Generic, index);
			colorToNewLayer(voxel);
		}
	}).setHelp("Move the voxels of the current selected palette index or the given index into a new layer");

	core::Command::registerCommand("abortaction", [&] (const core::CmdArgs& args) {
		_modifier.aabbStop();
	}).setHelp("Aborts the current modifier action");

	core::Command::registerCommand("thicken", [&] (const core::CmdArgs& args) {
		const int argc = args.size();
		int value = 1;
		if (argc >= 1) {
			value = core::string::toInt(args[0]);
		}
		thicken(value);
	}).setHelp("Thicken existing voxels");

	core::Command::registerCommand("setvoxelresolution", [&] (const core::CmdArgs& args) {
		const int argc = args.size();
		if (argc == 1) {
			const int size = core::string::toInt(args[0]);
			setGridResolution(size);
		} else {
			Log::warn("Expected to get a voxel resolution >= 1");
		}
	}).setHelp("");

	core::Command::registerCommand("setreferenceposition", [&] (const core::CmdArgs& args) {
		if (args.size() != 3) {
			Log::info("Expected to get x, y and z coordinates");
			return;
		}
		const int x = core::string::toInt(args[0]);
		const int y = core::string::toInt(args[1]);
		const int z = core::string::toInt(args[2]);
		setReferencePosition(glm::ivec3(x, y, z));
	}).setHelp("Set the reference position to the specified position");

	core::Command::registerCommand("movecursor", [this] (const core::CmdArgs& args) {
		if (args.size() < 3) {
			Log::info("Expected to get relative x, y and z coordinates");
			return;
		}
		const int x = core::string::toInt(args[0]);
		const int y = core::string::toInt(args[1]);
		const int z = core::string::toInt(args[2]);
		moveCursor(x, y, z);
	}).setHelp("Move the cursor by the specified offsets");

	core::Command::registerCommand("loadpalette", [this] (const core::CmdArgs& args) {
		if (args.size() != 1) {
			Log::info("Expected to get the palette NAME as part of palette-NAME.[png|lua]");
			return;
		}
		loadPalette(args[0]);
	}).setHelp("Load an existing palette by name. E.g. 'nippon'");

	core::Command::registerCommand("cursor", [this] (const core::CmdArgs& args) {
		if (args.size() < 3) {
			Log::info("Expected to get x, y and z coordinates");
			return;
		}
		const int x = core::string::toInt(args[0]);
		const int y = core::string::toInt(args[1]);
		const int z = core::string::toInt(args[2]);
		setCursorPosition(glm::ivec3(x, y, z), true);
	}).setHelp("Set the cursor to the specified position");

	core::Command::registerCommand("setreferencepositiontocursor", [&] (const core::CmdArgs& args) {
		setReferencePosition(cursorPosition());
	}).setHelp("Set the reference position to the current cursor position").setBindingContext(BindingContext::Model);

	core::Command::registerCommand("resize", [this] (const core::CmdArgs& args) {
		const int argc = args.size();
		if (argc == 1) {
			const int size = core::string::toInt(args[0]);
			resize(glm::ivec3(size));
		} else if (argc == 3) {
			glm::ivec3 size;
			for (int i = 0; i < argc; ++i) {
				size[i] = core::string::toInt(args[i]);
			}
			resize(size);
		} else {
			resize(glm::ivec3(1));
		}
	}).setHelp("Resize your volume about given x, y and z size");

	core::Command::registerActionButton("shift", _gizmo).setBindingContext(BindingContext::Scene);
	core::Command::registerCommand("shift", [&] (const core::CmdArgs& args) {
		const int argc = args.size();
		if (argc != 3) {
			Log::info("Expected to get x, y and z values");
			return;
		}
		const int x = core::string::toInt(args[0]);
		const int y = core::string::toInt(args[1]);
		const int z = core::string::toInt(args[2]);
		shift(x, y, z);
	}).setHelp("Shift the volume by the given values");

	core::Command::registerCommand("center_referenceposition", [&] (const core::CmdArgs& args) {
		const glm::ivec3& refPos = referencePosition();
		_layerMgr.foreachGroupLayer([&] (int layerId) {
			const auto* v = _volumeRenderer.volume(layerId);
			if (v == nullptr) {
				return;
			}
			const voxel::Region& region = v->region();
			const glm::ivec3& center = region.getCenter();
			const glm::ivec3& delta = refPos - center;
			shift(layerId, delta);
		});
	}).setHelp("Center the current active layers at the reference position");

	core::Command::registerCommand("center_origin", [&] (const core::CmdArgs& args) {
		_layerMgr.foreachGroupLayer([&] (int layerId) {
			const auto* v = _volumeRenderer.volume(layerId);
			if (v == nullptr) {
				return;
			}
			const voxel::Region& region = v->region();
			const glm::ivec3& delta = -region.getCenter();
			shift(layerId, delta);
		});
		setReferencePosition(glm::zero<glm::ivec3>());
	}).setHelp("Center the current active layers at the origin");

	core::Command::registerCommand("move", [&] (const core::CmdArgs& args) {
		const int argc = args.size();
		if (argc != 3) {
			Log::info("Expected to get x, y and z values");
			return;
		}
		const int x = core::string::toInt(args[0]);
		const int y = core::string::toInt(args[1]);
		const int z = core::string::toInt(args[2]);
		move(x, y, z);
	}).setHelp("Move the voxels inside the volume by the given values");

	core::Command::registerCommand("copy", [&] (const core::CmdArgs& args) {
		copy();
	}).setHelp("Copy selection");

	core::Command::registerCommand("paste", [&] (const core::CmdArgs& args) {
		paste(_referencePos);
	}).setHelp("Paste clipboard to current reference position");

	core::Command::registerCommand("pastecursor", [&] (const core::CmdArgs& args) {
		paste(_modifier.cursorPosition());
	}).setHelp("Paste clipboard to current cursor position");

	core::Command::registerCommand("cut", [&] (const core::CmdArgs& args) {
		cut();
	}).setHelp("Cut selection");

	core::Command::registerCommand("undo", [&] (const core::CmdArgs& args) {
		undo();
	}).setHelp("Undo your last step");

	core::Command::registerCommand("redo", [&] (const core::CmdArgs& args) {
		redo();
	}).setHelp("Redo your last step");

	core::Command::registerCommand("rotate", [&] (const core::CmdArgs& args) {
		if (args.size() < 3) {
			Log::info("Expected to get x, y and z angles in degrees"
					" and optionally a boolean to rotate around the reference position");
			return;
		}
		const int x = core::string::toInt(args[0]);
		const int y = core::string::toInt(args[1]);
		const int z = core::string::toInt(args[2]);
		bool rotateAroundReferencePosition = false;
		if (args.size() >= 4) {
			rotateAroundReferencePosition = core::string::toBool(args[3]);
		}
		rotate(x, y, z, true, rotateAroundReferencePosition);
	}).setHelp("Rotate scene by the given angles (in degree)");

	core::Command::registerCommand("layermerge", [&] (const core::CmdArgs& args) {
		int layer1;
		int layer2;
		if (args.size() == 2) {
			layer1 = core::string::toInt(args[0]);
			layer2 = core::string::toInt(args[1]);
		} else {
			layer1 = _layerMgr.activeLayer();
			// FIXME: this layer id might be an empty slot
			layer2 = layer1 + 1;
		}
		merge(layer1, layer2);
	}).setHelp("Merged two given layers or active layer with the one below");

	core::Command::registerCommand("layerdetails", [&] (const core::CmdArgs& args) {
		for (int idx = 0; idx < (int)_layerMgr.layers().size(); ++idx) {
			const Layer& layer = _layerMgr.layer(idx);
			if (!layer.valid) {
				continue;
			}
			Log::info("Layer %i:", idx);
			Log::info(" - name:    %s", layer.name.c_str());
			Log::info(" - visible: %s", layer.visible ? "true" : "false");
			const voxel::RawVolume* volume = _volumeRenderer.volume(idx);
			core_assert_always(volume != nullptr);
			const voxel::Region& region = volume->region();
			Log::info(" - region:");
			Log::info("   - mins:   %i:%i:%i", region.getLowerX(), region.getLowerY(), region.getLowerZ());
			Log::info("   - maxs:   %i:%i:%i", region.getUpperX(), region.getUpperY(), region.getUpperZ());
			Log::info("   - cells:  %i:%i:%i", region.getWidthInCells(), region.getHeightInCells(), region.getDepthInCells());
			Log::info("   - voxels: %i:%i:%i", region.getWidthInVoxels(), region.getHeightInVoxels(), region.getDepthInVoxels());
		}
	}).setHelp("Show details to all layers");

	core::Command::registerCommand("animate", [&] (const core::CmdArgs& args) {
		if (args.empty()) {
			Log::info("Usage: animate <framedelaymillis> <0|1>");
			Log::info("framedelay of 0 will stop the animation, too");
			return;
		}
		if (args.size() == 2) {
			if (!core::string::toBool(args[1])) {
				_animationSpeed = 0.0;
				return;
			}
		}
		_animationSpeed = core::string::toDouble(args[0]) / 1000.0;
	}).setHelp("Animate all visible layers with the given delay in millis between the frames");

	core::Command::registerCommand("setcolor", [&] (const core::CmdArgs& args) {
		if (args.size() != 1) {
			Log::info("Usage: setcolor <index>");
			return;
		}
		const uint8_t index = core::string::toInt(args[0]);
		const voxel::Voxel voxel = voxel::createVoxel(voxel::VoxelType::Generic, index);
		_modifier.setCursorVoxel(voxel);
	}).setHelp("Use the given index to select the color from the current palette");

	core::Command::registerCommand("setcolorrgb", [&] (const core::CmdArgs& args) {
		if (args.size() != 3) {
			Log::info("Usage: setcolorrgb <red> <green> <blue> (color range 0-255)");
			return;
		}
		const int red = core::string::toInt(args[0]);
		const int green = core::string::toInt(args[1]);
		const int blue = core::string::toInt(args[2]);
		glm::vec4 color(red / 255.0f, green / 255.0, blue / 255.0, 1.0f);
		const voxel::MaterialColorArray& materialColors = voxel::getMaterialColors();
		const int index = core::Color::getClosestMatch(color, materialColors);
		const voxel::Voxel voxel = voxel::createVoxel(voxel::VoxelType::Generic, index);
		_modifier.setCursorVoxel(voxel);
	}).setHelp("Set the current selected color by finding the closest rgb match in the palette");

	core::Command::registerCommand("pickcolor", [&] (const core::CmdArgs& args) {
		// during mouse movement, the current cursor position might be at an air voxel (this
		// depends on the mode you are editing in), thus we should use the cursor voxel in
		// that case
		if (_traceViaMouse && !voxel::isAir(_hitCursorVoxel.getMaterial())) {
			_modifier.setCursorVoxel(_hitCursorVoxel);
			return;
		}
		// resolve the voxel via cursor position. This allows to use also get the proper
		// result if we moved the cursor via keys (and thus might have skipped tracing)
		const glm::ivec3& cursorPos = _modifier.cursorPosition();
		const voxel::Voxel& voxel = modelVolume()->voxel(cursorPos);
		if (!voxel::isAir(voxel.getMaterial())) {
			_modifier.setCursorVoxel(voxel);
		}
	}).setHelp("Pick the current selected color from current cursor voxel");

	core::Command::registerCommand("replacecolor", [&] (const core::CmdArgs& args) {
		if (args.size() != 2) {
			Log::info("Usage: replacecolor <current-color-index> <new-color-index>");
			return;
		}
		const uint8_t oldIndex = core::string::toInt(args[0]);
		const int newIndex = core::string::toInt(args[1]);
		replaceColor(oldIndex, newIndex);
	}).setHelp("Replace a particular palette index with another index - if target is -1 is will be removed");

	core::Command::registerCommand("randomsimilarcolor", [&] (const core::CmdArgs& args) {
		if (args.size() < 1) {
			Log::info("Usage: randomsimilarcolor <color-index> [density] [colors]");
			return;
		}
		int colorIndex = core::string::toInt(args[0]);
		if (colorIndex == -1) {
			colorIndex = _modifier.cursorVoxel().getColor();
		}
		const int density = (glm::max)(1, args.size() >= 2 ? core::string::toInt(args[1]) : 4);
		const int colors = (glm::max)(1, args.size() >= 3 ? core::string::toInt(args[2]) : 4);
		randomSimilarColor(colorIndex, density, colors);
	}).setHelp("Replace a particular palette index with another random and similar index");

	core::Command::registerCommand("mirror", [&] (const core::CmdArgs& args) {
		if (args.size() != 1) {
			Log::info("Usage: mirror <axis:x,y,z>");
			return;
		}
		const char axisChar = args[0][0];
		math::Axis axis = math::Axis::X;
		if (axisChar == 'y') {
			axis = math::Axis::Y;
		} else if (axisChar == 'z') {
			axis = math::Axis::Z;
		}
		mirror(axis);
	}).setHelp("Mirror the selected layers around the given axis");

	core::Command::registerCommand("lock", [&] (const core::CmdArgs& args) {
		if (args.size() != 1) {
			Log::info("Usage: lock <axis:x,y,z>");
			return;
		}
		const char axisChar = args[0][0];
		math::Axis axis = math::Axis::X;
		if (axisChar == 'y') {
			axis = math::Axis::Y;
		} else if (axisChar == 'z') {
			axis = math::Axis::Z;
		}
		const bool unlock = (_lockedAxis & axis) == axis;
		setLockedAxis(axis, unlock);
	}).setHelp("Toggle locked mode for the given axis at the current cursor position");

	core::Command::registerCommand("lockx", [&] (const core::CmdArgs& args) {
		const math::Axis axis = math::Axis::X;
		const bool unlock = (_lockedAxis & axis) == axis;
		setLockedAxis(axis, unlock);
	}).setHelp("Toggle locked mode for the x axis at the current cursor position");

	core::Command::registerCommand("locky", [&] (const core::CmdArgs& args) {
		const math::Axis axis = math::Axis::Y;
		const bool unlock = (_lockedAxis & axis) == axis;
		setLockedAxis(axis, unlock);
	}).setHelp("Toggle locked mode for the y axis at the current cursor position");

	core::Command::registerCommand("lockz", [&] (const core::CmdArgs& args) {
		const math::Axis axis = math::Axis::Z;
		const bool unlock = (_lockedAxis & axis) == axis;
		setLockedAxis(axis, unlock);
	}).setHelp("Toggle locked mode for the z axis at the current cursor position");

	core::Command::registerCommand("centerplane", [&] (const core::CmdArgs& args) {
		modifier().setCenterMode(!modifier().centerMode());
	}).setHelp("Toggle center plane building");
}

void SceneManager::mirror(math::Axis axis) {
	_layerMgr.foreachGroupLayer([&] (int layerId) {
		auto* model = _volumeRenderer.volume(layerId);
		if (model == nullptr) {
			return;
		}
		voxel::RawVolume* newVolume = voxel::mirrorAxis(model, axis);
		voxel::Region r = newVolume->region();
		r.accumulate(model->region());
		setNewVolume(layerId, newVolume);
		modified(layerId, r);
	});
}

void SceneManager::toggleEditMode() {
	if (_editMode == EditMode::Model) {
		_editMode = EditMode::Scene;
		_modifier.aabbStop();
	} else if (_editMode == EditMode::Scene) {
		_editMode = EditMode::Model;
		_gizmo.resetMode();
	}
	setGizmoPosition();
	// don't abort or toggle any other mode
}

void SceneManager::setVoxelsForCondition(std::function<voxel::Voxel()> voxel, std::function<bool(const voxel::Voxel&)> condition) {
	// TODO: only change selection
	_layerMgr.foreachGroupLayer([&] (int layerId) {
		auto* v = _volumeRenderer.volume(layerId);
		if (v == nullptr) {
			return;
		}
		glm::ivec3 modifiedMins((std::numeric_limits<int>::max)() / 2);
		glm::ivec3 modifiedMaxs((std::numeric_limits<int>::min)() / 2);
		const int cnt = voxelutil::visitVolume(*v, [&] (int32_t x, int32_t y, int32_t z, const voxel::Voxel&) {
			if (!v->setVoxel(x, y, z, voxel())) {
				return;
			}

			modifiedMins.x = core_min(modifiedMins.x, x);
			modifiedMins.y = core_min(modifiedMins.y, y);
			modifiedMins.z = core_min(modifiedMins.z, z);

			modifiedMaxs.x = core_max(modifiedMaxs.x, x);
			modifiedMaxs.y = core_max(modifiedMaxs.y, y);
			modifiedMaxs.z = core_max(modifiedMaxs.z, z);
		}, condition);
		if (cnt > 0) {
			modified(layerId, voxel::Region{modifiedMins, modifiedMaxs});
			Log::debug("Modified %i voxels", cnt);
		}
	});
}

bool SceneManager::randomSimilarColor(uint8_t oldIndex, uint8_t density, uint8_t colorCount) {
	struct OnlyParticularIndexDensityVisitCondition {
		const uint8_t _index;
		const int _density;
		int _cnt = 0;
		OnlyParticularIndexDensityVisitCondition(uint8_t index, int density) :
				_index(index), _density(density) {
		}
		inline bool operator() (const voxel::Voxel& voxel) {
			if (voxel.getColor() == _index) {
				++_cnt;
				return _cnt % _density == 0;
			}
			return false;
		}
	};
	voxel::MaterialColorArray colors = voxel::getMaterialColors();
	const glm::vec4 color = colors[oldIndex];
	voxel::MaterialColorIndices newColorIndices;
	newColorIndices.resize(colorCount);
	int maxColorIndices = 0;
	auto colorIter = colors.begin();
	std::advance(colorIter, oldIndex);
	colors.erase(colorIter);
	for (; maxColorIndices < colorCount; ++maxColorIndices) {
		const int index = core::Color::getClosestMatch(color, colors);
		if (index <= 0) {
			break;
		}
		const glm::vec4& c = colors[index];
		const int materialIndex = core::Color::getClosestMatch(c, voxel::getMaterialColors());
		auto iter = colors.begin();
		std::advance(iter, index);
		colors.erase(iter);
		newColorIndices[maxColorIndices] = materialIndex;
	}
	if (maxColorIndices <= 0) {
		return false;
	}
	math::Random random;
	const OnlyParticularIndexDensityVisitCondition condition(oldIndex, density);
	setVoxelsForCondition([&] () { return voxel::createVoxel(voxel::VoxelType::Generic, newColorIndices[random.random(0, maxColorIndices - 1)]); }, condition);
	return true;
}

void SceneManager::replaceColor(uint8_t oldIndex, int newIndex) {
	struct OnlyParticularIndexVisitCondition {
		const uint8_t _index;
		OnlyParticularIndexVisitCondition(uint8_t index) :
				_index(index) {
		}
		inline bool operator() (const voxel::Voxel& voxel) const {
			return voxel.getColor() == _index;
		}
	};
	const voxel::Voxel voxel = newIndex < 0 ? voxel::Voxel() : voxel::createVoxel(voxel::VoxelType::Generic, newIndex);
	const OnlyParticularIndexVisitCondition condition(oldIndex);
	setVoxelsForCondition([&] () { return voxel; }, condition);
}

bool SceneManager::init() {
	++_initialized;
	if (_initialized > 1) {
		Log::debug("Already initialized");
		return true;
	}

	const char *paletteName = core::Var::getSafe(cfg::VoxEditLastPalette)->strVal().c_str();
	const io::FilesystemPtr& filesystem = io::filesystem();
	const io::FilePtr& paletteFile = filesystem->open(core::string::format("palette-%s.png", paletteName));
	if (!voxel::initMaterialColors(paletteFile, io::FilePtr())) {
		Log::warn("Failed to initialize the palette data for %s, falling back to default", paletteName);
		if (!voxel::initDefaultMaterialColors()) {
			Log::error("Failed to initialize the palette data");
			return false;
		}
	}

	if (!_gizmo.init()) {
		Log::error("Failed to initialize the gizmo");
		return false;
	}
	if (!_mementoHandler.init()) {
		Log::error("Failed to initialize the memento handler");
		return false;
	}
	if (!_volumeRenderer.init()) {
		Log::error("Failed to initialize the volume renderer");
		return false;
	}
	if (!_shapeRenderer.init()) {
		Log::error("Failed to initialize the shape renderer");
		return false;
	}
	if (!_gridRenderer.init()) {
		Log::error("Failed to initialize the grid renderer");
		return false;
	}
	if (!_layerMgr.init()) {
		Log::error("Failed to initialize the layer manager");
		return false;
	}
	if (!_modifier.init()) {
		Log::error("Failed to initialize the modifier");
		return false;
	}
	if (!_volumeCache.init()) {
		Log::error("Failed to initialize the volume cache");
		return false;
	}
	if (!_animationRenderer.init()) {
		Log::error("Failed to initialize the character renderer");
		return false;
	}
	_animationRenderer.setClearColor(core::Color::Clear);
	const auto& meshCache = std::make_shared<voxelformat::MeshCache>();
	_animationCache = std::make_shared<animation::AnimationCache>(meshCache);
	if (!_animationCache->init()) {
		Log::error("Failed to initialize the character mesh cache");
		return false;
	}

	_layerMgr.registerListener(this);

	_autoSaveSecondsDelay = core::Var::get(cfg::VoxEditAutoSaveSeconds, "180");
	_ambientColor = core::Var::get(cfg::VoxEditAmbientColor, "0.2 0.2 0.2");
	_diffuseColor = core::Var::get(cfg::VoxEditDiffuseColor, "1.0 1.0 1.0");
	_cameraZoomSpeed = core::Var::get(cfg::VoxEditCameraZoomSpeed, "10.0");
	const core::TimeProviderPtr& timeProvider = core::App::getInstance()->timeProvider();
	_lastAutoSave = timeProvider->tickSeconds();

	for (int i = 0; i < lengthof(_planeMeshIndex); ++i) {
		_planeMeshIndex[i] = -1;
	}

	_shapeBuilder.clear();
	_shapeBuilder.setColor(core::Color::alpha(core::Color::SteelBlue, 0.8f));
	_shapeBuilder.sphere(8, 6, 0.5f);
	_referencePointMesh = _shapeRenderer.create(_shapeBuilder);

	_lockedAxis = math::Axis::None;
	return true;
}

void SceneManager::animate(double nowSeconds) {
	if (_animationSpeed <= 0.0) {
		return;
	}
	if (_nextFrameSwitch <= nowSeconds) {
		_nextFrameSwitch = nowSeconds + _animationSpeed;
		const int layers = (int)_layerMgr.layers().size();
		const int roundTrip = layers + _currentAnimationLayer;
		for (int idx = _currentAnimationLayer + 1; idx < roundTrip; ++idx) {
			const Layer& layer = _layerMgr.layer(idx % layers);
			if (layer.valid) {
				 _layerMgr.hideLayer(_currentAnimationLayer, true);
				_currentAnimationLayer = idx % layers;
				_layerMgr.hideLayer(_currentAnimationLayer, false);
				return;
			}
		}
	}
}

void SceneManager::zoom(video::Camera& camera, float level) const {
	const float cameraSpeed = _cameraZoomSpeed->floatVal();
	const float value = cameraSpeed * level;
	const float targetDistance = glm::clamp(camera.targetDistance() + value, 0.0f, 1000.0f);
	if (targetDistance > 1.0f) {
		const glm::vec3& moveDelta = glm::backward * value;
		camera.move(moveDelta);
		camera.setTargetDistance(targetDistance);
	}
}

void SceneManager::update(double nowSeconds) {
	_volumeRenderer.update();
	for (int i = 0; i < lengthof(DIRECTIONS); ++i) {
		if (!_move[i].pressed()) {
			continue;
		}
		_move[i].execute(nowSeconds, 0.125, [&] () {
			const Direction& dir = DIRECTIONS[i];
			moveCursor(dir.x, dir.y, dir.z);
		});
	}
	if (_zoomIn.pressed()) {
		_zoomIn.execute(nowSeconds, 0.02, [&] () {
			if (_camera != nullptr) {
				zoom(*_camera, 1.0f);
			}
		});
	} else if (_zoomOut.pressed()) {
		_zoomOut.execute(nowSeconds, 0.02, [&] () {
			if (_camera != nullptr) {
				zoom(*_camera, -1.0f);
			}
		});
	}

	if (_camera != nullptr) {
		if (_modelSpace->boolVal() != _gizmo.isModelSpace()) {
			const bool newModelSpaceState = _modelSpace->boolVal();
			if (newModelSpaceState) {
				Log::info("switch to model space");
				_gizmo.setModelSpace();
			} else {
				Log::info("switch to world space");
				_gizmo.setWorldSpace();
			}
			setGizmoPosition();
		}

		if (_editMode == EditMode::Scene) {
			_gizmo.updateMode(*_camera, _mouseCursor);
			_gizmo.execute(nowSeconds, [&] (const glm::vec3& deltaMovement, render::GizmoMode mode) {
				executeGizmoAction(glm::ivec3(deltaMovement), mode);
			});
		}
	}
	if (_ambientColor->isDirty()) {
		_volumeRenderer.setAmbientColor(_ambientColor->vec3Val());
		_ambientColor->markClean();
	}
	if (_diffuseColor->isDirty()) {
		_volumeRenderer.setDiffuseColor(_diffuseColor->vec3Val());
		_diffuseColor->markClean();
	}
	animate(nowSeconds);
	autosave();
	extractVolume();
}

void SceneManager::shutdown() {
	--_initialized;
	if (_initialized != 0) {
		return;
	}

	if (_copy) {
		delete _copy;
		_copy = nullptr;
	}

	std::vector<voxel::RawVolume*> old = _volumeRenderer.shutdown();
	for (voxel::RawVolume* v : old) {
		delete v;
	}

	_volumeCache.shutdown();
	_mementoHandler.shutdown();
	_modifier.shutdown();
	_layerMgr.unregisterListener(this);
	_layerMgr.shutdown();
	_gizmo.shutdown();
	_shapeRenderer.shutdown();
	_shapeBuilder.shutdown();
	_gridRenderer.shutdown();
	_mementoHandler.clearStates();
	_animationRenderer.shutdown();
	_animationCache->shutdown();
	_character.shutdown();
	_bird.shutdown();

	_referencePointMesh = -1;
	_aabbMeshIndex = -1;
}

animation::AnimationEntity& SceneManager::animationEntity() {
	if (_entityType == animation::AnimationSettings::Type::Character) {
		return _character;
	}
	return _bird;
}

bool SceneManager::saveAnimationEntity(const char *name) {
	_dirty = false;
	// TODO: race and gender
	const core::String& chrName = core::string::format("chr/human-male-%s", name);
	const core::String& luaFilePath = animation::luaFilename(chrName.c_str());
	const core::String luaDir(core::string::extractPath(luaFilePath));
	io::filesystem()->createDir(luaDir);
	const io::FilePtr& luaFile = io::filesystem()->open(luaFilePath, io::FileMode::SysWrite);
	const animation::AnimationSettings& animSettings = animationEntity().animationSettings();
	if (saveAnimationEntityLua(animSettings, animationEntity().skeletonAttributes(), name, luaFile)) {
		Log::info("Wrote lua script: %s", luaFile->name().c_str());
	}

	const voxedit::Layers& layers = _layerMgr.layers();
	const size_t layerAmount = layers.size();
	for (size_t i = 0; i < layerAmount; ++i) {
		const voxel::RawVolume* v = volume(i);
		if (v == nullptr) {
			continue;
		}
		const voxedit::Layer& l = layers[i];
		const core::String& value = l.metadataById("type");
		if (value.empty()) {
			const core::String& unknown = core::string::format("%i-%s-%s.vox", (int)i, l.name.c_str(), name);
			Log::warn("No type metadata found on layer %i. Saving to %s", (int)i, unknown.c_str());
			if (!saveLayer(i, unknown)) {
				Log::warn("Failed to save unknown layer to %s", unknown.c_str());
				_dirty = true;
			}
			continue;
		}
		const int characterMeshTypeId = core::string::toInt(value);
		const core::String& fullPath = animSettings.fullPath(characterMeshTypeId, name);
		if (!saveLayer(i, fullPath)) {
			Log::warn("Failed to save type %i to %s", characterMeshTypeId, fullPath.c_str());
			_dirty = true;
		}
	}

	return true;
}

bool SceneManager::loadAnimationEntity(const core::String& luaFile) {
	const core::String& lua = io::filesystem()->load(luaFile);
	animation::AnimationSettings settings;
	if (!animation::loadAnimationSettings(lua, settings, nullptr)) {
		Log::warn("Failed to initialize the animation settings for %s", luaFile.c_str());
		return false;
	}
	_entityType = settings.type();
	if (_entityType == animation::AnimationSettings::Type::Max) {
		Log::warn("Failed to detect the entity type for %s", luaFile.c_str());
		return false;
	}

	if (!animationEntity().initSettings(lua)) {
		Log::warn("Failed to initialize the animation settings and attributes for %s", luaFile.c_str());
	}

	voxel::VoxelVolumes volumes;
	if (!_volumeCache.getVolumes(animationEntity().animationSettings(), volumes)) {
		return false;
	}

	// create a new scene and in case of successfully loading all the anim related
	// stuff, we will then delete the first layer again.
	newScene(true, "entity", voxel::Region());
	int layersAdded = 0;
	for (size_t i = 0u; i < volumes.size(); ++i) {
		const auto& v = volumes[i];
		if (v.volume == nullptr) {
			continue;
		}
		const bool visible = layersAdded == 0;
		const int layerId = _layerMgr.addLayer(v.name.c_str(), visible, v.volume, v.pivot);
		if (layerId != -1) {
			++layersAdded;
			_layerMgr.addMetadata(layerId, {{"type", core::string::format("%i", (int)i)}});
		}
	}
	if (layersAdded > 0) {
		_layerMgr.deleteLayer(0, true);
		_layerMgr.findNewActiveLayer();
	}

	resetSceneState();
	_animationUpdate = true;
	_editMode = EditMode::Animation;
	return true;
}

bool SceneManager::extractVolume() {
	core_trace_scoped(SceneManagerExtract);
	const size_t n = _extractRegions.size();
	if (n <= 0) {
		return false;
	}
	Log::info("Extract the meshes for %i regions", (int)n);
	for (size_t i = 0; i < n; ++i) {
		const voxel::Region& region = _extractRegions[i].region;
		if (!_volumeRenderer.extractRegion(_extractRegions[i].layer, region)) {
			Log::error("Failed to extract the model mesh");
		}
		Log::debug("Extract layer %i", _extractRegions[i].layer);
		voxel::logRegion("Extraction", region);
	}
	_extractRegions.clear();
	return true;
}

void SceneManager::noise(int octaves, float lacunarity, float frequency, float gain, voxelgenerator::noise::NoiseType type) {
	math::Random random;
	const int layerId = _layerMgr.activeLayer();
	voxel::RawVolumeWrapper wrapper(volume(layerId));
	if (voxelgenerator::noise::generate(wrapper, octaves, lacunarity, frequency, gain, type, random) <= 0) {
		Log::warn("Could not generate noise");
		return;
	}
	// if the same noise is generated again - the wrapper doesn't override anything, but the voxels are still somehow placed.
	if (!wrapper.dirtyRegion().isValid()) {
		return;
	}
	modified(layerId, wrapper.dirtyRegion());
}

void SceneManager::lsystem(const core::String &axiom, const std::vector<voxelgenerator::lsystem::Rule> &rules, float angle, float length,
		float width, float widthIncrement, int iterations, float leavesRadius) {
	math::Random random;
	const int layerId = _layerMgr.activeLayer();
	voxel::RawVolumeWrapper wrapper(volume(layerId));
	voxelgenerator::lsystem::generate(wrapper, referencePosition(), axiom, rules, angle, length, width, widthIncrement, iterations, random, leavesRadius);
	modified(layerId, wrapper.dirtyRegion());
}

void SceneManager::createTree(const voxelgenerator::TreeContext& ctx) {
	math::Random random(ctx.cfg.seed);
	const int layerId = _layerMgr.activeLayer();
	voxel::RawVolumeWrapper wrapper(volume(layerId));
	voxelgenerator::tree::createTree(wrapper, ctx, random);
	modified(layerId, wrapper.dirtyRegion());
}

void SceneManager::setReferencePosition(const glm::ivec3& pos) {
	_referencePos = pos;
	const glm::vec3 posAligned(_referencePos.x + 0.5f, _referencePos.y + 0.5f, _referencePos.z + 0.5f);
	_referencePointModelMatrix = glm::translate(posAligned);
}

void SceneManager::moveCursor(int x, int y, int z) {
	glm::ivec3 p = cursorPosition();
	const int res = gridRenderer().gridResolution();
	p.x += x * res;
	p.y += y * res;
	p.z += z * res;
	setCursorPosition(p, true);
	_hitCursorVoxel = modelVolume()->voxel(cursorPosition());
	_traceViaMouse = false;
}

void SceneManager::setCursorPosition(glm::ivec3 pos, bool force) {
	const voxel::RawVolume* v = modelVolume();
	if (v == nullptr) {
		return;
	}

	const int res = gridRenderer().gridResolution();
	const voxel::Region& region = v->region();
	const glm::ivec3& mins = region.getLowerCorner();
	const glm::ivec3 delta = pos - mins;
	if (delta.x % res != 0) {
		pos.x = mins.x + (delta.x / res) * res;
	}
	if (delta.y % res != 0) {
		pos.y = mins.y + (delta.y / res) * res;
	}
	if (delta.z % res != 0) {
		pos.z = mins.z + (delta.z / res) * res;
	}
	const glm::ivec3& oldCursorPos = cursorPosition();
	if (!force) {
		if ((_lockedAxis & math::Axis::X) != math::Axis::None) {
			pos.x = oldCursorPos.x;
		}
		if ((_lockedAxis & math::Axis::Y) != math::Axis::None) {
			pos.y = oldCursorPos.y;
		}
		if ((_lockedAxis & math::Axis::Z) != math::Axis::None) {
			pos.z = oldCursorPos.z;
		}
	}

	if (!region.containsPoint(pos)) {
		pos = region.moveInto(pos.x, pos.y, pos.z);
	}
	if (oldCursorPos == pos) {
		return;
	}
	_modifier.setCursorPosition(pos, _result.hitFace);

	updateLockedPlane(math::Axis::X);
	updateLockedPlane(math::Axis::Y);
	updateLockedPlane(math::Axis::Z);
}

void SceneManager::setRenderAxis(bool renderAxis) {
	_renderAxis = renderAxis;
}

void SceneManager::setRenderLockAxis(bool renderLockAxis) {
	_renderLockAxis = renderLockAxis;
}

void SceneManager::setRenderShadow(bool shadow) {
	_renderShadow = shadow;
}

bool SceneManager::trace(bool force) {
	if (!_traceViaMouse) {
		return false;
	}
	if (_lastRaytraceX == _mouseCursor.x && _lastRaytraceY == _mouseCursor.y && !force) {
		return true;
	}
	if (_camera == nullptr) {
		return false;
	}
	const voxel::RawVolume* model = modelVolume();
	if (model == nullptr) {
		return false;
	}

	Log::debug("Execute new trace for %i:%i (%i:%i)",
			_mouseCursor.x, _mouseCursor.y, _lastRaytraceX, _lastRaytraceY);

	core_trace_scoped(EditorSceneOnProcessUpdateRay);
	_lastRaytraceX = _mouseCursor.x;
	_lastRaytraceY = _mouseCursor.y;

	const video::Ray& ray = _camera->mouseRay(_mouseCursor);
	const glm::vec3& dirWithLength = ray.direction * _camera->farPlane();
	static constexpr voxel::Voxel air;

	_result.didHit = false;
	_result.validPreviousPosition = false;
	_result.firstValidPosition = false;
	_result.direction = ray.direction;
	_result.hitFace = voxel::FaceNames::NoOfFaces;
	raycastWithDirection(model, ray.origin, dirWithLength, [&] (voxel::RawVolume::Sampler& sampler) {
		if (!_result.firstValidPosition && sampler.currentPositionValid()) {
			_result.firstPosition = sampler.position();
			_result.firstValidPosition = true;
		}

		if (sampler.voxel() != air) {
			_result.didHit = true;
			_result.hitVoxel = sampler.position();
			const glm::ivec3& dir = glm::ivec3(ray.origin) - _result.hitVoxel;
			if (dir.x < 0) {
				_result.hitFace = voxel::FaceNames::NegativeX;
			} else if (dir.x > 0) {
				_result.hitFace = voxel::FaceNames::PositiveX;
			} else if (dir.y < 0) {
				_result.hitFace = voxel::FaceNames::NegativeY;
			} else if (dir.y > 0) {
				_result.hitFace = voxel::FaceNames::PositiveY;
			} else if (dir.z < 0) {
				_result.hitFace = voxel::FaceNames::NegativeZ;
			} else if (dir.z > 0) {
				_result.hitFace = voxel::FaceNames::PositiveZ;
			}
			return false;
		}
		if (sampler.currentPositionValid()) {
			if (_lockedAxis != math::Axis::None) {
				const glm::ivec3& cursorPos = cursorPosition();
				if ((_lockedAxis & math::Axis::X) != math::Axis::None) {
					if (sampler.position()[0] == cursorPos[0]) {
						return false;
					}
				}
				if ((_lockedAxis & math::Axis::Y) != math::Axis::None) {
					if (sampler.position()[1] == cursorPos[1]) {
						return false;
					}
				}
				if ((_lockedAxis & math::Axis::Z) != math::Axis::None) {
					if (sampler.position()[2] == cursorPos[2]) {
						return false;
					}
				}
			}

			_result.validPreviousPosition = true;
			_result.previousPosition = sampler.position();
		}
		return true;
	});

	if (_modifier.modifierTypeRequiresExistingVoxel()) {
		if (_result.didHit) {
			setCursorPosition(_result.hitVoxel);
		} else if (_result.validPreviousPosition) {
			setCursorPosition(_result.previousPosition);
		}
	} else if (_result.validPreviousPosition) {
		setCursorPosition(_result.previousPosition);
	} else if (_result.didHit) {
		setCursorPosition(_result.hitVoxel);
	}

	if (_result.didHit) {
		_hitCursorVoxel = model->voxel(_result.hitVoxel);
	}

	return true;
}

void SceneManager::updateLockedPlane(math::Axis axis) {
	if (axis == math::Axis::None) {
		return;
	}
	const int index = getIndexForAxis(axis);
	int32_t& meshIndex = _planeMeshIndex[index];
	if ((_lockedAxis & axis) == math::Axis::None) {
		if (meshIndex != -1) {
			_shapeRenderer.deleteMesh(meshIndex);
			meshIndex = -1;
		}
		return;
	}

	const glm::vec4 colors[] = {
		core::Color::LightRed,
		core::Color::LightGreen,
		core::Color::LightBlue
	};
	updateShapeBuilderForPlane(_shapeBuilder, _volumeRenderer.region(), false, cursorPosition(), axis, core::Color::alpha(colors[index], 0.4f));
	_shapeRenderer.createOrUpdate(meshIndex, _shapeBuilder);
}

void SceneManager::setLockedAxis(math::Axis axis, bool unlock) {
	if (unlock) {
		_lockedAxis &= ~axis;
	} else {
		_lockedAxis |= axis;
	}
	updateLockedPlane(math::Axis::X);
	updateLockedPlane(math::Axis::Y);
	updateLockedPlane(math::Axis::Z);
}

void SceneManager::onLayerChanged(int layerId) {
	_mementoHandler.markUndo(layerId, _layerMgr.layer(layerId).name, nullptr, MementoType::LayerRenamed);
}

void SceneManager::onLayerDuplicate(int layerId) {
	const Layer& layer = _layerMgr.layer(layerId);
	const voxel::RawVolume* volume = _volumeRenderer.volume(layerId);
	_layerMgr.addLayer(layer.name.c_str(), true, new voxel::RawVolume(volume));
}

void SceneManager::onLayerSwapped(int layerId1, int layerId2) {
	// TODO: mementohandler
	if (!_volumeRenderer.swap(layerId1, layerId2)) {
		Log::error("Failed to swap volumes for layer %i and layer %i", layerId1, layerId2);
	}
}

void SceneManager::onLayerHide(int layerId) {
	_volumeRenderer.hide(layerId, true);
}

void SceneManager::onLayerShow(int layerId) {
	_volumeRenderer.hide(layerId, false);
}

void SceneManager::onActiveLayerChanged(int old, int active) {
	const voxel::RawVolume* volume = _volumeRenderer.volume(active);
	core_assert_always(volume != nullptr);
	const voxel::Region& region = volume->region();
	updateGridRenderer(region);
	updateAABBMesh();
	if (!region.containsPoint(referencePosition())) {
		glm::ivec3 center = region.getCenter();
		center.y = region.getLowerY();
		setReferencePosition(center);
	}
	if (!region.containsPoint(cursorPosition())) {
		setCursorPosition(volume->region().getCenter());
	}
	setGizmoPosition();
	resetLastTrace();
}

void SceneManager::setGizmoPosition() {
	if (_gizmo.isModelSpace()) {
		const voxel::Region& region = modelVolume()->region();
		_gizmo.setPosition(region.getLowerCornerf());
	} else {
		_gizmo.setPosition(glm::zero<glm::vec3>());
	}
}

void SceneManager::onLayerAdded(int layerId, const Layer& layer, voxel::RawVolume* volume, const voxel::Region& region) {
	core_trace_scoped(SceneManagerAddLayer);
	if (volume == nullptr) {
		const voxel::Region& newVolumeRegion = _volumeRenderer.region();
		volume = new voxel::RawVolume(newVolumeRegion);
	}
	Log::debug("Adding layer %i with name %s", layerId, layer.name.c_str());
	// Add two states here - one with the empty layer and one with the filled layer.
	// To always be able to return to the empty layer
	_mementoHandler.markLayerAdded(layerId, layer.name, volume);
	if (region.isValid()) {
		// the volume is maybe an old state and only needs to get updated in the modified region.
		setNewVolume(layerId, volume, false);
		queueRegionExtraction(layerId, region);
	} else {
		// update the whole volume
		setNewVolume(layerId, volume, true);
		queueRegionExtraction(layerId, volume->region());
	}
	setReferencePosition(layer.pivot);
	_volumeRenderer.hide(layerId, !layer.visible);
	_needAutoSave = true;
	_dirty = true;
	handleAnimationViewUpdate(layerId);
	// TODO: add layer meta data if we add a new layer for animations.
	//_layerMgr.addMetadata(layerId, {{"type", ""}});
}

void SceneManager::onLayerDeleted(int layerId, const Layer& layer) {
	voxel::RawVolume* v = _volumeRenderer.setVolume(layerId, nullptr);
	if (v != nullptr) {
		Log::debug("Deleted layer %i with name %s", layerId, layer.name.c_str());
		// Add two states here - one with the filled layer and one with the empty layer.
		// To always be able to return to the filled layer
		_mementoHandler.markLayerDeleted(layerId, layer.name, v);
		_volumeRenderer.update(layerId);
		_needAutoSave = true;
		_dirty = true;
		delete v;
		updateAABBMesh();
	}
}

bool SceneManager::empty() const {
	for (const auto& l : _layerMgr.layers()) {
		if (l.valid) {
			return false;
		}
	}
	return true;
}

}
