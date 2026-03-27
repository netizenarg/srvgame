#include "network/GameServer.hpp"

GameServer::GameServer(const WorkerGroupConfig& groupConfig, const ConfigManager& globalConfig)
    : ioContext_(groupConfig.threads),
      acceptor_(ioContext_),
      signals_(ioContext_),
      groupConfig_(groupConfig),
      globalConfig_(globalConfig),
      host_(groupConfig.host),
      port_(groupConfig.port),
      reuse_(groupConfig.reuse),
      ioThreads_(groupConfig.threads)
{
    // Set up SSL context if requested
    if (groupConfig.ssl.has_value()) {
        sslContext_ = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
        sslContext_->use_certificate_chain_file(groupConfig.ssl->certificate);
        sslContext_->use_private_key_file(groupConfig.ssl->private_key, asio::ssl::context::pem);
        if (!groupConfig.ssl->dh_params.empty()) {
            sslContext_->use_tmp_dh_file(groupConfig.ssl->dh_params);
        }
        // Additional SSL options can be set here
    }
}

GameServer::~GameServer() = default;

bool GameServer::Initialize() {
    try {
        asio::ip::tcp::endpoint endpoint(
            asio::ip::make_address(host_),
            port_
        );
        acceptor_.open(endpoint.protocol());
        if (reuse_) {
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            int optval = 1;
            if (setsockopt(acceptor_.native_handle(),
                SOL_SOCKET,
                SO_REUSEPORT,
                &optval,
                sizeof(optval)) < 0) {
                Logger::Error("Failed to set SO_REUSEPORT: {}", strerror(errno));
            }
        }
        acceptor_.bind(endpoint);
        acceptor_.listen(groupConfig_.max_connections);

        SetupSignalHandlers();
        Logger::Info("GameServer initialized for protocol '{}' on {}:{}",
                     groupConfig_.protocol, host_, port_);
        return true;
    } catch (const std::exception& e) {
        Logger::Critical("Failed to initialize server for protocol '{}': {}",
                         groupConfig_.protocol, e.what());
        return false;
    }
}

void GameServer::Run() {
    running_ = true;
    DoAccept();
    StartWorkerThreads();
    Logger::Info("GameServer started with {} IO threads for protocol '{}'",
                 ioThreads_, groupConfig_.protocol);
    ioContext_.run();
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    Logger::Info("GameServer run finished for protocol '{}'", groupConfig_.protocol);
}

void GameServer::DoAccept() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (!ec) {
                // Apply socket options
                if (groupConfig_.tcp_nodelay) {
                    asio::ip::tcp::no_delay option(true);
                    socket.set_option(option);
                }
                if (groupConfig_.send_buffer_size > 0) {
                    asio::socket_base::send_buffer_size option(groupConfig_.send_buffer_size);
                    socket.set_option(option);
                }
                if (groupConfig_.receive_buffer_size > 0) {
                    asio::socket_base::receive_buffer_size option(groupConfig_.receive_buffer_size);
                    socket.set_option(option);
                }

                if (groupConfig_.protocol == "binary") {
                    if (sessionFactory_) {
                        auto session = sessionFactory_(std::move(socket), sslContext_);
                        ConnectionManager::GetInstance().Start(session);
                        session->Start();
                    } else {
                        Logger::Error("No session factory set for binary protocol");
                    }
                } else if (groupConfig_.protocol == "websocket") {
                    if (webSocketFactory_) {
                        auto wsConn = webSocketFactory_(std::move(socket), sslContext_);
                        auto session = std::make_shared<WebSocketSession>(wsConn);
                        session->SetBinaryMessageHandler([world](uint16_t type, const std::vector<uint8_t>& data) {
                            world->HandleBinaryMessage(type, data); // Your world's binary handler
                        });
                        ConnectionManager::GetInstance().Start(session);
                        session->Start();
                    } else {
                        Logger::Error("No WebSocket factory set");
                    }
                } else {
                    Logger::Error("Unknown protocol: {}", groupConfig_.protocol);
                }
            } else {
                if (ec != asio::error::operation_aborted) {
                    Logger::Error("Accept error: {}", ec.message());
                } else {
                    Logger::Debug("Accept aborted during shutdown");
                }
            }
            if (running_) {
                DoAccept();
            }
        });
}

void GameServer::StartWorkerThreads() {
    for (int i = 0; i < ioThreads_ - 1; ++i) {
        workerThreads_.emplace_back([this]() {
            ioContext_.run();
        });
    }
}

void GameServer::SetupSignalHandlers() {
    signals_.add(SIGINT);
    signals_.add(SIGTERM);
    signals_.add(SIGQUIT);
    signals_.async_wait([this](std::error_code ec, int signal) {
        if (!ec) {
            Logger::Info("Received signal {}, shutting down...", signal);
            Shutdown();
        }
    });
}

void GameServer::Shutdown() {
    if (!running_) return;
    running_ = false;
    ConnectionManager::GetInstance().StopAll();
    signals_.cancel();
    acceptor_.close();
    ioContext_.stop();
    Logger::Info("GameServer shutdown initiated for protocol '{}'", groupConfig_.protocol);
}

void GameServer::SetSessionFactory(SessionFactory factory) {
    sessionFactory_ = std::move(factory);
}

void GameServer::SetWebSocketConnectionFactory(WebSocketFactory factory) {
    webSocketFactory_ = std::move(factory);
}
