/**
 * @file
 */

#include "ClientMessages_generated.h"
#include "ServerLoop.h"

#include "core/command/Command.h"
#include "core/Var.h"
#include "core/Log.h"
#include "core/App.h"
#include "io/Filesystem.h"
#include "cooldown/CooldownProvider.h"
#include "persistence/ConnectionPool.h"
#include "BackendModels.h"
#include "EventMgrModels.h"
#include "backend/entity/User.h"
#include "backend/entity/ai/AICommon.h"
#include "backend/network/UserConnectHandler.h"
#include "backend/network/UserConnectedHandler.h"
#include "backend/network/UserDisconnectHandler.h"
#include "backend/network/AttackHandler.h"
#include "backend/network/MoveHandler.h"
#include "core/command/CommandHandler.h"
#include "voxel/MaterialColor.h"
#include "eventmgr/EventMgr.h"
#include "stock/StockDataProvider.h"
#include "metric/UDPMetricSender.h"
using namespace std::chrono_literals;

namespace backend {

ServerLoop::ServerLoop(const WorldPtr& world, const persistence::DBHandlerPtr& dbHandler,
		const network::ServerNetworkPtr& network, const io::FilesystemPtr& filesystem,
		const EntityStoragePtr& entityStorage, const core::EventBusPtr& eventBus,
		const attrib::ContainerProviderPtr& containerProvider, const poi::PoiProviderPtr& poiProvider,
		const cooldown::CooldownProviderPtr& cooldownProvider, const eventmgr::EventMgrPtr& eventMgr,
		const stock::StockProviderPtr& stockDataProvider, const metric::IMetricSenderPtr& metricSender) :
		_network(network), _world(world),
		_entityStorage(entityStorage), _eventBus(eventBus), _attribContainerProvider(containerProvider),
		_poiProvider(poiProvider), _cooldownProvider(cooldownProvider), _eventMgr(eventMgr), _dbHandler(dbHandler),
		_stockDataProvider(stockDataProvider), _metric("server."), _metricSender(metricSender), _filesystem(filesystem) {
	_eventBus->subscribe<network::NewConnectionEvent>(*this);
	_eventBus->subscribe<network::DisconnectEvent>(*this);
	_eventBus->subscribe<metric::MetricEvent>(*this);
}

bool ServerLoop::addTimer(uv_timer_t* timer, uv_timer_cb cb, uint64_t repeatMillis, uint64_t initialDelayMillis) {
	timer->data = this;
	uv_timer_init(_loop, timer);
	return uv_timer_start(timer, cb, initialDelayMillis, repeatMillis) == 0;
}

void ServerLoop::onIdle(uv_idle_t* handle) {
	ServerLoop* loop = (ServerLoop*)handle->data;
	metric::Metric& metric = loop->_metric;

	const core::App* app = core::App::getInstance();
	metric.timing("frame.delta", app->deltaFrame());
	metric.gauge("uptime", app->lifetimeInSeconds() * 1000.0f);
}

#define regHandler(type, handler, ...) \
	r->registerHandler(network::EnumNameClientMsgType(type), std::make_shared<handler>(__VA_ARGS__));

bool ServerLoop::init() {
	_loop = new uv_loop_t;
	if (uv_loop_init(_loop) != 0) {
		Log::error("Failed to init event loop");
		return false;
	}
	if (!_metricSender->init()) {
		Log::warn("Failed to init metric sender");
	}
	if (!_metric.init(_metricSender)) {
		Log::warn("Failed to init metrics");
	}
	if (!_dbHandler->init()) {
		Log::error("Failed to init the dbhandler");
		return false;
	}
	if (!_dbHandler->createTable(db::UserModel())) {
		Log::error("Failed to create user table");
		return false;
	}
	if (!_dbHandler->createTable(db::StockModel())) {
		Log::error("Failed to create stock table");
		return false;
	}
	if (!_dbHandler->createTable(db::InventoryModel())) {
		Log::error("Failed to create stock table");
		return false;
	}
	if (!_eventMgr->init()) {
		Log::error("Failed to init event manager");
		return false;
	}
	if (!_network->init()) {
		Log::error("Failed to init the network");
		return false;
	}

	const core::VarPtr& port = core::Var::getSafe(cfg::ServerPort);
	const core::VarPtr& host = core::Var::getSafe(cfg::ServerHost);
	const core::VarPtr& maxclients = core::Var::getSafe(cfg::ServerMaxClients);
	if (!_network->bind(port->intVal(), host->strVal(), maxclients->intVal(), 2)) {
		Log::error("Failed to bind the server socket on %s:%i", host->strVal().c_str(), port->intVal());
		return false;
	}
	Log::info("Server socket is up at %s:%i", host->strVal().c_str(), port->intVal());

	const std::string& cooldowns = _filesystem->load("cooldowns.lua");
	if (!_cooldownProvider->init(cooldowns)) {
		Log::error("Failed to load the cooldown configuration: %s", _cooldownProvider->error().c_str());
		return false;
	}

	const std::string& stockLuaString = _filesystem->load("stock.lua");
	if (!_stockDataProvider->init(stockLuaString)) {
		Log::error("Failed to load the stock configuration: %s", _stockDataProvider->error().c_str());
		return false;
	}

	const std::string& attributes = _filesystem->load("attributes.lua");
	if (!_attribContainerProvider->init(attributes)) {
		Log::error("Failed to load the attributes: %s", _attribContainerProvider->error().c_str());
		return false;
	}

	const network::ProtocolHandlerRegistryPtr& r = _network->registry();
	regHandler(network::ClientMsgType::UserConnect, UserConnectHandler, _network, _entityStorage);
	regHandler(network::ClientMsgType::UserConnected, UserConnectedHandler);
	regHandler(network::ClientMsgType::UserDisconnect, UserDisconnectHandler);
	regHandler(network::ClientMsgType::Attack, AttackHandler);
	regHandler(network::ClientMsgType::Move, MoveHandler);

	if (!voxel::initDefaultMaterialColors()) {
		Log::error("Failed to initialize the palette data");
		return false;
	}

	if (!_world->init()) {
		Log::error("Failed to init the world");
		return false;
	}

	addTimer(&_poiTimer, [] (uv_timer_t* handle) {
		ServerLoop* loop = (ServerLoop*)handle->data;
		loop->_poiProvider->update(handle->repeat);
	}, 1000);
	addTimer(&_worldTimer, [] (uv_timer_t* handle) {
		ServerLoop* loop = (ServerLoop*)handle->data;
		loop->_world->update(handle->repeat);
	}, 1000);
	addTimer(&_entityStorageTimer, [] (uv_timer_t* handle) {
		ServerLoop* loop = (ServerLoop*)handle->data;
		loop->_entityStorage->update(handle->repeat);
	}, 275);

	_idleTimer.data = this;
	if (uv_idle_init(_loop, &_idleTimer) != 0) {
		Log::warn("Couldn't init the idle timer");
		return false;
	}
	uv_idle_start(&_idleTimer, onIdle);

	if (!_input.init(_loop)) {
		Log::warn("Could not init console input");
	}

	return true;
}

void ServerLoop::shutdown() {
	_world->shutdown();
	_dbHandler->shutdown();
	_metricSender->shutdown();
	_metric.shutdown();
	_input.shutdown();
	_network->shutdown();
	uv_timer_stop(&_poiTimer);
	uv_timer_stop(&_worldTimer);
	uv_timer_stop(&_spawnMgrTimer);
	uv_timer_stop(&_entityStorageTimer);
	uv_idle_stop(&_idleTimer);
	uv_tty_reset_mode();
	if (_loop != nullptr) {
		uv_loop_close(_loop);
		delete _loop;
		_loop = nullptr;
	}
}

void ServerLoop::update(long dt) {
	core_trace_scoped(ServerLoop);
	uv_run(_loop, UV_RUN_NOWAIT);
	_network->update();
	std::this_thread::sleep_for(1ms);
}

void ServerLoop::onEvent(const metric::MetricEvent& event) {
	metric::MetricEventType type = event.type();
	switch (type) {
	case metric::MetricEventType::Count:
		_metric.count(event.key().c_str(), event.value(), event.tags());
		break;
	case metric::MetricEventType::Gauge:
		_metric.gauge(event.key().c_str(), (uint32_t)event.value(), event.tags());
		break;
	case metric::MetricEventType::Timing:
		_metric.timing(event.key().c_str(), (uint32_t)event.value(), event.tags());
		break;
	case metric::MetricEventType::Histogram:
		_metric.histogram(event.key().c_str(), (uint32_t)event.value(), event.tags());
		break;
	case metric::MetricEventType::Meter:
		_metric.meter(event.key().c_str(), event.value(), event.tags());
		break;
	}
}

void ServerLoop::onEvent(const network::DisconnectEvent& event) {
	ENetPeer* peer = event.peer();
	Log::info("disconnect peer: %u", peer->connectID);
	User* user = reinterpret_cast<User*>(peer->data);
	if (user == nullptr) {
		return;
	}
	user->triggerLogout();
}

void ServerLoop::onEvent(const network::NewConnectionEvent& event) {
	Log::info("new connection - waiting for login request from %u", event.peer()->connectID);
	_metric.increment("count.user");
}

}
