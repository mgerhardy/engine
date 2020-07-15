/**
 * @file
 */

#pragma once

#include "voxelutil/Picking.h"
#include "voxel/RawVolume.h"
#include "voxelgenerator/TreeContext.h"
#include "voxelgenerator/LSystem.h"
#include "voxelgenerator/NoiseGenerator.h"
#include "voxelrender/RawVolumeRenderer.h"
#include "voxelformat/VoxFileFormat.h"
#include "video/ShapeBuilder.h"
#include "animation/chr/Character.h"
#include "animation/animal/bird/Bird.h"
#include "animation/AnimationRenderer.h"
#include "anim/VolumeCache.h"
#include "render/ShapeRenderer.h"
#include "render/GridRenderer.h"
#include "render/Gizmo.h"
#include "core/Var.h"
#include "core/Singleton.h"
#include "core/command/ActionButton.h"
#include "math/Axis.h"
#include "MementoHandler.h"
#include "layer/LayerListener.h"
#include "layer/Layer.h"
#include "layer/LayerManager.h"
#include "modifier/ModifierType.h"
#include "modifier/Modifier.h"
#include <vector>
#include <functional>

namespace voxedit {

/**
 * @brief Move directions for the cursor
 */
static constexpr struct Direction {
	const char *postfix;
	int x;
	int y;
	int z;
} DIRECTIONS[] = {
	{"left",      1,  0,  0},
	{"right",    -1,  0,  0},
	{"up",        0,  1,  0},
	{"down",      0, -1,  0},
	{"forward",   0,  0,  1},
	{"backward",  0,  0, -1}
};

enum class EditMode {
	// Edit a model volume (voxels)
	Model,
	// Edit an animation (lua)
	Animation,
	// Edit the scene (volume positions, rotations, ...)
	Scene
};

/**
 * @note The data is shared across all viewports
 */
class SceneManager : public core::IComponent, public LayerListener {
private:
	voxelrender::RawVolumeRenderer _volumeRenderer;
	anim::VolumeCache _volumeCache;
	render::GridRenderer _gridRenderer;
	video::ShapeBuilder _shapeBuilder;
	render::ShapeRenderer _shapeRenderer;
	MementoHandler _mementoHandler;
	LayerManager _layerMgr;
	Modifier _modifier;
	voxel::RawVolume* _copy = nullptr;
	render::Gizmo _gizmo;
	EditMode _editMode = EditMode::Model;

	animation::AnimationSettings::Type _entityType = animation::AnimationSettings::Type::Max;
	animation::Character _character;
	animation::Bird _bird;

	animation::AnimationRenderer _animationRenderer;
	animation::AnimationCachePtr _animationCache;
	// if we are in animation mode and we modified the meshes, we have to
	// update the cache with the current volume. this is the layer id
	// that we have to perform the update for - or -1 if all layers were
	// touched
	int _animationLayerDirtyState = -1;
	int _animationIdx = 0;
	bool _animationUpdate = false;

	/**
	 * The @c video::Camera instance of the currently active @c Viewport
	 */
	video::Camera* _camera = nullptr;

	int32_t _aabbMeshIndex = -1;
	int32_t _referencePointMesh = -1;
	glm::mat4 _referencePointModelMatrix { 1.0f };

	glm::ivec3 _referencePos;

	core::VarPtr _autoSaveSecondsDelay;
	core::VarPtr _ambientColor;
	core::VarPtr _diffuseColor;
	core::VarPtr _cameraZoomSpeed;
	core::VarPtr _modelSpace;

	math::Axis _lockedAxis = math::Axis::None;

	struct DirtyRegion {
		voxel::Region region;
		int layer;
	};
	using RegionQueue = std::vector<DirtyRegion>;
	RegionQueue _extractRegions;
	void queueRegionExtraction(int layerId, const voxel::Region& region);

	bool _dirty = false;
	// this is basically the same as the dirty state, but we stop
	// auto-saving once we saved a dirty state
	bool _needAutoSave = false;

	bool _renderShadow = true;
	bool _renderAxis = true;
	bool _renderLockAxis = true;

	core::String _lastFilename;
	uint64_t _lastAutoSave = 0u;

	int32_t _planeMeshIndex[3] = {-1, -1, -1};

	int _lastRaytraceX = -1;
	int _lastRaytraceY = -1;

	// layer animation speed
	double _animationSpeed = 0.0;
	int _currentAnimationLayer = 0;
	double _nextFrameSwitch = 0.0;

	int _initialized = 0;
	int _size = 128;
	glm::ivec2 _mouseCursor { 0 };

	bool _traceViaMouse = true;

	core::ActionButton _move[lengthof(DIRECTIONS)];

	core::ActionButton _zoomIn;
	core::ActionButton _zoomOut;

	voxel::PickResult _result;
	// existing voxel under the cursor
	voxel::Voxel _hitCursorVoxel;

	voxel::RawVolume* modelVolume();
	/**
	 * @brief Assumes that the current active scene is a fresh scene, no undo states
	 * are left, scene is no longer dirty and so on.
	 */
	void resetSceneState();
	void handleAnimationViewUpdate(int layerId);
	bool setNewVolume(int idx, voxel::RawVolume* volume, bool deleteMesh = true);
	bool setNewVolumes(const voxel::VoxelVolumes& volumes);
	void autosave();
	void setReferencePosition(const glm::ivec3& pos);
	void updateGridRenderer(const voxel::Region& region);

	void animate(double nowSeconds);
	/**
	 * @brief Move the cursor relative by the given steps in each direction
	 */
	void moveCursor(int x, int y, int z);

	void colorToNewLayer(const voxel::Voxel voxelColor);
	void thicken(int amount);
	void crop();
	void scale(int layerId);
	void resize(const glm::ivec3& size);
	int size() const;

	/**
	 * @brief Merge two layers and extend the smaller one
	 */
	bool merge(int layerId1, int layerId2);

	void undo();
	void redo();

	void copy();
	void paste(const glm::ivec3& pos);
	void cut();

	void rotate(int layerId, const glm::ivec3& angle, bool increaseSize = false, bool rotateAroundReferencePosition = false);
	void rotate(int angleX, int angleY, int angleZ, bool increaseSize = false, bool rotateAroundReferencePosition = false);

	/**
	 * @brief Move the voxels inside the volume regions
	 */
	void move(int layerId, const glm::ivec3& m);

	/**
	 * @brief Shift the whole volume by the given voxel amount
	 */
	void shift(int layerId, const glm::ivec3& m);

	void executeGizmoAction(const glm::ivec3& delta, render::GizmoMode mode);
	void toggleEditMode();

	void zoom(video::Camera& camera, float level) const;

	bool saveLayers(const core::String& dir);
	bool saveLayer(int layerId, const core::String& file);
	bool extractVolume();
	void updateLockedPlane(math::Axis axis);
	void setVoxelsForCondition(std::function<voxel::Voxel()> voxel, std::function<bool(const voxel::Voxel&)> condition);
	void replaceColor(uint8_t oldIndex, int newIndex);
	bool randomSimilarColor(uint8_t oldIndex, uint8_t density, uint8_t colorCount);
	void mirror(math::Axis axis);
	void updateAABBMesh();
public:
	~SceneManager();

	void construct() override;
	bool init() override;
	void update(double nowSeconds);
	void shutdown() override;

	/**
	 * @return The full region of all layers of the whole scene
	 */
	voxel::Region region() const;

	void setGizmoPosition();

	void setMousePos(int x, int y);

	void setActiveCamera(video::Camera* camera);

	const voxel::Voxel& hitCursorVoxel() const;

	const glm::ivec3& cursorPosition() const;
	/**
	 * @brief Sets the cursor to the given position in the volume
	 * @note The locked axes are taken into account here and the given position might not be
	 * the final position of the cursor
	 * @param[in] force If @c true it will ignore the locked axes and still set the position
	 */
	void setCursorPosition(glm::ivec3 pos, bool force = false);

	const glm::ivec3& referencePosition() const;

	void modified(int layerId, const voxel::Region& modifiedRegion, bool markUndo = true);
	voxel::RawVolume* volume(int idx);

	/**
	 * @brief Import a heightmap in the current layer of the scene
	 * @param[in] file The image file to import as heightmap.
	 * @note The first component is used as height value. In most cases the R channel.
	 */
	bool importHeightmap(const core::String& file);
	/**
	 * @brief Import an image as a plane in a new layer of the scene.
	 * @note There is a total max dimension of the texture that is supported.
	 * @param[in] file The image file path to load
	 */
	bool importAsPlane(const core::String& file);
	/**
	 * @brief Import a new palette from the given image file
	 * @note The amount of colors in the image may not exceed 256
	 * @param[in] file The image file path
	 */
	bool importPalette(const core::String& file);
	/**
	 * @param[in] paletteName The name of the palette
	 * @note The name is extended to a filename like this @c palette-<paletteName>.[lua.png]
	 */
	bool loadPalette(const core::String& paletteName);
	/**
	 * @brief Fill the volume with the given noise type
	 */
	void noise(int octaves, float persistence, float lacunarity, float gain, voxelgenerator::noise::NoiseType type);
	/**
	 * @brief Create a new procgen tree
	 */
	void createTree(const voxelgenerator::TreeContext& ctx);

	void lsystem(const core::String &axiom, const std::vector<voxelgenerator::lsystem::Rule> &rules, float angle,
			float length, float width, float widthIncrement, int iterations, float leavesRadius);

	/**
	 * @brief Save the volume data to the given file
	 * @param[in] file The file to store the volume data in. The file extension defines the volume format.
	 * @param[in] autosave @c true if this is an auto save action, @c false otherwise. This has e.g. an
	 * influence on the dirty state handling of the scene.
	 */
	bool save(const core::String& file, bool autosave = false);
	/**
	 * @brief Loads a volume from the given file
	 * @param[in] file The file to load. The volume format is determined by the file extension.
	 */
	bool load(const core::String& file);
	bool loadAnimationEntity(const core::String& luaFile);
	bool saveAnimationEntity(const char *name);

	/**
	 * @brief Shift the whole volume by the given voxel amount
	 */
	void shift(int x, int y, int z);
	/**
	 * @brief Move the voxels inside the volume regions
	 */
	void move(int x, int y, int z);

	/**
	 * @brief Import an existing model
	 * @note Placed relative to the reference position in the current scene
	 */
	bool prefab(const core::String& file);

	bool newScene(bool force, const core::String& name, const voxel::Region& region);

	/**
	 * @return @c true if the scene was modified and not saved yet
	 */
	bool dirty() const;

	/**
	 * @return @c true if the scene is completely empty
	 */
	bool empty() const;
	EditMode editMode() const;

	static const uint8_t RenderScene = 1u << 0u;
	static const uint8_t RenderUI    = 1u << 1u;
	static const uint8_t RenderAll   = RenderScene | RenderUI;

	/**
	 * @brief Performs the rendering for each @c Viewport instance
	 */
	void render(const video::Camera& camera, uint8_t renderMask = RenderAll);
	void renderAnimation(const video::Camera& camera);

	bool trace(bool force = false);
	void resetLastTrace();

	math::Axis lockedAxis() const;
	void setLockedAxis(math::Axis axis, bool unlock);
	void setRenderAxis(bool renderAxis);
	void setRenderLockAxis(bool renderLockAxis);
	void setRenderShadow(bool shadow);
	bool setGridResolution(int resolution);

	// component access
	const LayerManager& layerMgr() const;
	LayerManager& layerMgr();
	const Modifier& modifier() const;
	Modifier& modifier();
	const MementoHandler& mementoHandler() const;
	MementoHandler& mementoHandler();
	const voxelrender::RawVolumeRenderer& renderer() const;
	voxelrender::RawVolumeRenderer& renderer();
	render::GridRenderer& gridRenderer();
	animation::SkeletonAttribute* skeletonAttributes();
	animation::AnimationEntity& animationEntity();
	const render::Gizmo& gizmo() const;

	// LayerListener
	void onLayerChanged(int layerId) override;
	void onLayerDuplicate(int layerId) override;
	void onLayerSwapped(int layerId1, int layerId2) override;
	void onLayerHide(int layerId) override;
	void onLayerShow(int layerId) override;
	void onActiveLayerChanged(int old, int active) override;
	void onLayerAdded(int layerId, const Layer& layer, voxel::RawVolume* volume, const voxel::Region& region) override;
	void onLayerDeleted(int layerId, const Layer& layer) override;
};

inline void SceneManager::setActiveCamera(video::Camera* camera) {
	_camera = camera;
	resetLastTrace();
}

inline const render::Gizmo& SceneManager::gizmo() const {
	return _gizmo;
}

inline voxelrender::RawVolumeRenderer& SceneManager::renderer() {
	return _volumeRenderer;
}

inline const voxelrender::RawVolumeRenderer& SceneManager::renderer() const {
	return _volumeRenderer;
}

inline math::Axis SceneManager::lockedAxis() const {
	return _lockedAxis;
}

inline const MementoHandler& SceneManager::mementoHandler() const {
	return _mementoHandler;
}

inline MementoHandler& SceneManager::mementoHandler() {
	return _mementoHandler;
}

inline render::GridRenderer& SceneManager::gridRenderer() {
	return _gridRenderer;
}

inline animation::SkeletonAttribute* SceneManager::skeletonAttributes() {
	return &animationEntity().skeletonAttributes();
}

inline bool SceneManager::dirty() const {
	return _dirty;
}

inline int SceneManager::size() const {
	return _size;
}

inline EditMode SceneManager::editMode() const {
	return _editMode;
}

inline const voxel::Voxel& SceneManager::hitCursorVoxel() const {
	return _hitCursorVoxel;
}

inline const glm::ivec3& SceneManager::cursorPosition() const {
	return _modifier.cursorPosition();
}

inline const glm::ivec3& SceneManager::referencePosition() const {
	return _referencePos;
}

inline const LayerManager& SceneManager::layerMgr() const {
	return _layerMgr;
}

inline LayerManager& SceneManager::layerMgr() {
	return _layerMgr;
}

inline const Modifier& SceneManager::modifier() const {
	return _modifier;
}

inline Modifier& SceneManager::modifier() {
	return _modifier;
}

inline SceneManager& sceneMgr() {
	return core::Singleton<SceneManager>::getInstance();
}

}
