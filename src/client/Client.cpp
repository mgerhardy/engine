/**
 * @file
 */

#include "ClientMessages_generated.h"
#include "ServerMessages_generated.h"
#include "Client.h"
#include "voxel/ClientPager.h"
#include "video/TextureAtlasRenderer.h"
#include "voxelformat/MeshCache.h"
#include "voxelrender/CachedMeshRenderer.h"
#include "core/command/Command.h"
#include "core/GLM.h"
#include "core/io/Filesystem.h"
#include "core/Color.h"
#include "core/Password.h"
#include "network/IMsgProtocolHandler.h"
#include "network/AttribUpdateHandler.h"
#include "network/AuthFailedHandler.h"
#include "network/EntityRemoveHandler.h"
#include "network/EntitySpawnHandler.h"
#include "network/EntityUpdateHandler.h"
#include "network/UserSpawnHandler.h"
#include "network/UserInfoHandler.h"
#include "network/VarUpdateHandler.h"
#include "network/StartCooldownHandler.h"
#include "network/StopCooldownHandler.h"
#include "audio/SoundManager.h"
#include "voxel/MaterialColor.h"
#include "core/metric/Metric.h"
#include "core/TimeProvider.h"
#include "core/SharedPtr.h"
#include "commonlua/LUA.h"
#include "ClientLUA.h"
#include <SDL.h>
#include <engine-config.h>

Client::Client(const metric::MetricPtr& metric, const animation::AnimationCachePtr& animationCache,
		const stock::StockDataProviderPtr& stockDataProvider,
		const network::ClientNetworkPtr& network, const voxelworld::WorldMgrPtr& world,
		const client::ClientPagerPtr& worldPager,
		const network::ClientMessageSenderPtr& messageSender,
		const core::EventBusPtr& eventBus, const core::TimeProviderPtr& timeProvider,
		const io::FilesystemPtr& filesystem, const voxelformat::VolumeCachePtr& volumeCache,
		const voxelformat::MeshCachePtr& meshCache,
		const video::TexturePoolPtr& texturePool,
		const voxelrender::CachedMeshRendererPtr& meshRenderer,
		const video::TextureAtlasRendererPtr& textureAtlasRenderer,
		const audio::SoundManagerPtr& soundManager) :
		Super(metric, filesystem, eventBus, timeProvider, texturePool, meshRenderer, textureAtlasRenderer), _animationCache(animationCache),
		_network(network), _worldMgr(world), _clientPager(worldPager), _messageSender(messageSender), _movement(soundManager),
		_stockDataProvider(stockDataProvider), _volumeCache(volumeCache),
		_meshCache(meshCache), _camera(_worldRenderer), _soundManager(soundManager) {
	init(ORGANISATION, "client");
}

Client::~Client() {
}

frontend::ClientEntityPtr Client::getEntity(frontend::ClientEntityId id) const {
	return _worldRenderer.entityMgr().getEntity(id);
}

frontend::ClientEntityId Client::id() const {
	if (!_player) {
		return -1;
	}
	return _player->id();
}

void Client::sendTriggerAction() {
	if (!_action.popTriggerAction()) {
		return;
	}
	_messageSender->sendClientMessage(_actionFbb, network::ClientMsgType::TriggerAction, CreateTriggerAction(_actionFbb).Union());
}

void Client::sendMovement() {
	const network::MoveDirection moveMask = _movement.moveMask();

	// TODO: we can't use the camera, as we are aiming for a freelook mode, where the players' angles might be different from the camera's
	const video::Camera& camera = _camera.camera();
	const float pitch = camera.pitch();
	const float yaw = camera.horizontalYaw();
	glm::vec2 moveAngles(pitch, yaw);

	if (_lastMoveMask != moveMask || !glm::all(glm::epsilonEqual(moveAngles, _lastMoveAngles, 0.0001f))) {
		_lastMoveMask = moveMask;
		_lastMoveAngles = moveAngles;
		_messageSender->sendClientMessage(_moveFbb, network::ClientMsgType::Move, CreateMove(_moveFbb, moveMask, moveAngles.x, moveAngles.y).Union(), 0u);
	}
}

void Client::onEvent(const network::DisconnectEvent& event) {
	_network->destroy();
	pushWindow("login");
	pushWindow("disconnect_info");
}

void Client::onEvent(const network::NewConnectionEvent& event) {
	flatbuffers::FlatBufferBuilder fbb;
	const core::String& email = core::Var::getSafe(cfg::ClientEmail)->strVal();
	const core::String& password = core::Var::getSafe(cfg::ClientPassword)->strVal();
	Log::info("Trying to log into the server with %s", email.c_str());
	const core::String& pwhash = core::pwhash(password, "TODO");
	_messageSender->sendClientMessage(fbb, network::ClientMsgType::UserConnect,
			network::CreateUserConnect(fbb, fbb.CreateString(email.c_str(), email.size()),
			fbb.CreateString(pwhash.c_str(), pwhash.size())).Union());
}

void Client::onEvent(const voxelworld::WorldCreatedEvent& event) {
	Log::info("world created");
	pushWindow("hud");
}

core::AppState Client::onConstruct() {
	core::AppState state = Super::onConstruct();

	_soundManager->construct();
	_volumeCache->construct();
	_movement.construct();
	_action.construct();
	_camera.construct();
	_meshCache->construct();

	core::Var::get(cfg::ClientPort, SERVER_PORT, "Server port");
	core::Var::get(cfg::ClientHost, SERVER_HOST, "Server hostname or ip");
	core::Var::get(cfg::ClientEmail, "");
	core::Var::get(cfg::ClientName, "noname", core::CV_BROADCAST);
	core::Var::get(cfg::ClientPassword, "");
	_chunkUrl = core::Var::get(cfg::ServerChunkBaseUrl, "");
	_seed = core::Var::get(cfg::ServerSeed, "");
	_rotationSpeed = core::Var::getSafe(cfg::ClientMouseRotationSpeed);
	core::VarPtr meshSize = core::Var::get(cfg::VoxelMeshSize, "32", core::CV_READONLY);
	meshSize->setVal(glm::clamp(meshSize->intVal(), 16, 128));
	_worldRenderer.construct();

	core::Command::registerCommand("cl_attrib", [this] (const core::CmdArgs &args) {
		if (!_player) {
			Log::info("You must be logged into a gameserver");
			return;
		}
		const attrib::ShadowAttributes& attrib = _player->attrib();
		for (int i = (int)attrib::Type::MIN + 1; i < (int)attrib::Type::MAX; ++i) {
			const attrib::Type type = (attrib::Type)i;
			const double curVal = attrib.current(type);
			const double maxVal = attrib.max(type);
			Log::info("%s: %f/%f", network::toString(type, ::network::EnumNamesAttribType()), curVal, maxVal);
		}
	}).setHelp("Print all player attributes");

	return state;
}

#define regHandler(type, handler, ...) \
	r->registerHandler(network::EnumNameServerMsgType(type), std::make_shared<handler>(__VA_ARGS__));

core::AppState Client::onInit() {
	eventBus()->subscribe<network::NewConnectionEvent>(*this);
	eventBus()->subscribe<network::DisconnectEvent>(*this);
	eventBus()->subscribe<voxelworld::WorldCreatedEvent>(*this);

	const network::ProtocolHandlerRegistryPtr& r = _network->registry();
	regHandler(network::ServerMsgType::AttribUpdate, AttribUpdateHandler);
	regHandler(network::ServerMsgType::EntitySpawn, EntitySpawnHandler);
	regHandler(network::ServerMsgType::EntityRemove, EntityRemoveHandler);
	regHandler(network::ServerMsgType::EntityUpdate, EntityUpdateHandler);
	regHandler(network::ServerMsgType::UserSpawn, UserSpawnHandler);
	regHandler(network::ServerMsgType::AuthFailed, AuthFailedHandler);
	regHandler(network::ServerMsgType::StartCooldown, StartCooldownHandler);
	regHandler(network::ServerMsgType::StopCooldown, StopCooldownHandler);
	regHandler(network::ServerMsgType::VarUpdate, VarUpdateHandler);
	regHandler(network::ServerMsgType::UserInfo, UserInfoHandler);

	core::AppState state = Super::onInit();
	if (state != core::AppState::Running) {
		return state;
	}

	video::enableDebug(video::DebugSeverity::Medium);

	if (!_meshCache->init()) {
		Log::error("Failed to initialize mesh cache");
		return core::AppState::InitFailure;
	}

	if (!_network->init()) {
		Log::error("Failed to initialize network layer");
		return core::AppState::InitFailure;
	}

	if (!_movement.init()) {
		Log::error("Failed to initialize movement controller");
		return core::AppState::InitFailure;
	}

	if (!_action.init()) {
		Log::error("Failed to initialize action controller");
		return core::AppState::InitFailure;
	}

	if (!_stockDataProvider->init(filesystem()->load("stock.lua"))) {
		Log::error("Failed to initialize stock data provider: %s", _stockDataProvider->error().c_str());
		return core::AppState::InitFailure;
	}

	_camera.init(glm::ivec2(0), frameBufferDimension(), windowDimension());

	if (!_animationCache->init()) {
		Log::error("Failed to initialize character cache");
		return core::AppState::InitFailure;
	}

	if (!_soundManager->init()) {
		Log::warn("Failed to initialize the sound manager");
	}

	if (!voxel::initDefaultMaterialColors()) {
		Log::error("Failed to initialize the palette data");
		return core::AppState::InitFailure;
	}

	if (!_volumeCache->init()) {
		Log::error("Failed to initialize volume cache");
		return core::AppState::InitFailure;
	}

	if (!_worldMgr->init()) {
		Log::error("Failed to initialize world manager");
		return core::AppState::InitFailure;
	}

	if (!_floorResolver.init(_worldMgr)) {
		Log::error("Failed to initialize floor resolver");
		return core::AppState::InitFailure;
	}

	if (!_clientPager->init(_chunkUrl->strVal())) {
		Log::error("Failed to initialize client pager");
		return core::AppState::InitFailure;
	}

	if (!_worldRenderer.init(_worldMgr->volumeData(), glm::ivec2(0), frameBufferDimension())) {
		Log::error("Failed to initialize world renderer");
		return core::AppState::InitFailure;
	}

	rootWindow("main");

	return state;
}

void Client::configureLUA(lua::LUA& lua) {
	Super::configureLUA(lua);
	_lua.newGlobalData<Client>("clientpointer", this);
	clientlua_init(lua.state());
}

void Client::initUIConfig(struct nk_convert_config& config) {
	Super::initUIConfig(config);
}

void Client::initUISkin() {
	Super::initUISkin();
}

void Client::beforeUI() {
	Super::beforeUI();

	if (_chunkUrl->isDirty()) {
		_clientPager->init(_chunkUrl->strVal());
		_chunkUrl->markClean();
	}
	if (_seed->isDirty()) {
		_seed->markClean();
		const unsigned int seed = _seed->intVal();
		Log::info("Initialize for seed %u", seed);
		_clientPager->setSeed(seed);
		_eventBus->publish(voxelworld::WorldCreatedEvent());
	}
	if (_player) {
		const video::Camera& camera = _camera.camera();
		_movement.update(_deltaFrameSeconds, camera.horizontalYaw(), _player, [&] (const glm::ivec3& pos, int maxWalkHeight) {
			return _floorResolver.findWalkableFloor(pos, maxWalkHeight);
		});
		_action.update(_nowSeconds, _player);
		const double speed = _player->attrib().current(attrib::Type::SPEED);
		_camera.update(_player->position(), _nowSeconds, _deltaFrameSeconds, speed);
		_worldRenderer.extractMeshes(camera);
		_worldRenderer.update(camera, _deltaFrameSeconds);
		_worldRenderer.renderWorld(camera);
	}

	sendVars();
}

void Client::sendVars() const {
	std::vector<core::VarPtr> vars;
	core::Var::visitDirtyBroadcast([&vars] (const core::VarPtr& var) {
		vars.push_back(var);
	});
	if (vars.empty()) {
		return;
	}
	static flatbuffers::FlatBufferBuilder fbb;
	auto fbbVars = fbb.CreateVector<flatbuffers::Offset<network::Var>>(vars.size(),
		[&] (size_t i) {
			const core::String& sname = vars[i]->name();
			const core::String& svalue = vars[i]->strVal();
			auto name = fbb.CreateString(sname.c_str(), sname.size());
			auto value = fbb.CreateString(svalue.c_str(), svalue.size());
			return network::CreateVar(fbb, name, value);
		});
	_messageSender->sendClientMessage(fbb, network::ClientMsgType::VarUpdate,
			network::CreateVarUpdate(fbb, fbbVars).Union());
}

core::AppState Client::onCleanup() {
	Log::info("shutting down the client");
	eventBus()->unsubscribe<network::NewConnectionEvent>(*this);
	eventBus()->unsubscribe<network::DisconnectEvent>(*this);
	eventBus()->unsubscribe<voxelworld::WorldCreatedEvent>(*this);

	Log::info("disconnect");
	disconnect();

	_soundManager->shutdown();
	Log::info("shutting down the client components");
	_stockDataProvider->shutdown();
	Log::info("shutting down the character cache");
	_animationCache->shutdown();
	Log::info("shutting down the world renderer");
	_worldRenderer.shutdown();
	Log::info("shutting down the world");
	_worldMgr->shutdown();
	_floorResolver.shutdown();
	_player = frontend::ClientEntityPtr();
	Log::info("shutting down the network");
	_network->shutdown();
	_movement.shutdown();
	_action.shutdown();
	_camera.shutdown();
	_meshCache->shutdown();
	Log::info("shutting down the volume cache");
	_volumeCache->shutdown();
	Log::info("everything was shut down");

	return Super::onCleanup();
}

core::AppState Client::onRunning() {
	const core::AppState state = Super::onRunning();
	if (_network->isConnected()) {
		const float pitch = _mouseRelativePos.y;
		const float turn = _mouseRelativePos.x;
		_camera.rotate(pitch, turn, _rotationSpeed->floatVal());
		sendMovement();
		sendTriggerAction();
	}
	if (state == core::AppState::Running) {
		_network->update();
		_soundManager->update();
	}

	return state;
}

void Client::onWindowResize(int windowWidth, int windowHeight) {
	Super::onWindowResize(windowWidth, windowHeight);
	_camera.init(glm::ivec2(0), frameBufferDimension(), windowDimension());
}

void Client::authFailed() {
	pushWindow("auth_failed");
}

void Client::disconnect() {
	if (!_network->isConnecting() && !_network->isConnected()) {
		return;
	}

	_player = frontend::ClientEntityPtr();
	flatbuffers::FlatBufferBuilder fbb;
	_messageSender->sendClientMessage(fbb, network::ClientMsgType::UserDisconnect, network::CreateUserDisconnect(fbb).Union());
	_network->disconnect();
}

void Client::entitySpawn(frontend::ClientEntityId id, network::EntityType type, float orientation, const glm::vec3& pos, animation::Animation animation) {
	Log::info("Entity %li spawned at pos %f:%f:%f (type %i)", id, pos.x, pos.y, pos.z, (int)type);
	const frontend::ClientEntityPtr& entity = core::make_shared<frontend::ClientEntity>(_stockDataProvider, _animationCache, id, type, pos, orientation);
	entity->setAnimation(animation, true);
	_worldRenderer.entityMgr().addEntity(entity);
}

void Client::entityRemove(frontend::ClientEntityId id) {
	_worldRenderer.entityMgr().removeEntity(id);
}

void Client::spawn(frontend::ClientEntityId id, const char *name, const glm::vec3& pos, float orientation) {
	Log::info("User %li (%s) logged in at pos %f:%f:%f with orientation: %f", id, name, pos.x, pos.y, pos.z, orientation);
	_camera.setTarget(pos);

	// TODO: get map id from server
	_clientPager->setMapId(1);
	const network::EntityType type = network::EntityType::PLAYER;
	_player = core::make_shared<frontend::ClientEntity>(_stockDataProvider, _animationCache, id, type, pos, orientation);
	_worldRenderer.entityMgr().addEntity(_player);
	_worldRenderer.extractMeshes(_camera.camera());

	flatbuffers::FlatBufferBuilder fbb;
	_messageSender->sendClientMessage(fbb, network::ClientMsgType::UserConnected,
			network::CreateUserConnected(fbb).Union());
}

bool Client::connect(uint16_t port, const core::String& hostname) {
	if (hostname.empty()) {
		Log::error("No hostname given");
		return false;
	}
	ENetPeer* peer = _network->connect(port, hostname);
	if (peer == nullptr) {
		Log::error("Failed to connect to server %s:%i", hostname.c_str(), port);
		return false;
	}

	peer->data = this;
	Log::info("Connecting to server %s:%i", hostname.c_str(), port);
	return true;
}

int main(int argc, char *argv[]) {
	const voxelformat::MeshCachePtr& meshCache = std::make_shared<voxelformat::MeshCache>();
	const voxelrender::CachedMeshRendererPtr& meshRenderer = core::make_shared<voxelrender::CachedMeshRenderer>(meshCache);
	const video::TextureAtlasRendererPtr& textureAtlasRenderer = core::make_shared<video::TextureAtlasRenderer>();
	const animation::AnimationCachePtr& animationCache = std::make_shared<animation::AnimationCache>(meshCache);
	const core::EventBusPtr& eventBus = std::make_shared<core::EventBus>();
	const voxelformat::VolumeCachePtr& volumeCache = std::make_shared<voxelformat::VolumeCache>();
	const core::TimeProviderPtr& timeProvider = std::make_shared<core::TimeProvider>();
	const io::FilesystemPtr& filesystem = std::make_shared<io::Filesystem>();
	const network::ProtocolHandlerRegistryPtr& protocolHandlerRegistry = std::make_shared<network::ProtocolHandlerRegistry>();
	const network::ClientNetworkPtr& network = std::make_shared<network::ClientNetwork>(protocolHandlerRegistry, eventBus);
	const network::ClientMessageSenderPtr& messageSender = std::make_shared<network::ClientMessageSender>(network);
	const client::ClientPagerPtr& pager = core::make_shared<client::ClientPager>();
	const voxelworld::WorldMgrPtr& world = std::make_shared<voxelworld::WorldMgr>(pager);
	const metric::MetricPtr& metric = std::make_shared<metric::Metric>();
	const stock::StockDataProviderPtr& stockDataProvider = std::make_shared<stock::StockDataProvider>();
	const video::TexturePoolPtr& texturePool = std::make_shared<video::TexturePool>(filesystem);
	const audio::SoundManagerPtr& soundMgr = core::make_shared<audio::SoundManager>(filesystem);
	Client app(metric, animationCache, stockDataProvider, network, world, pager, messageSender,
			eventBus, timeProvider, filesystem, volumeCache, meshCache, texturePool, meshRenderer, textureAtlasRenderer,
			soundMgr);
	return app.startMainLoop(argc, argv);
}
