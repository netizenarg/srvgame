#include <memory>
#include <stdexcept>

#include "database/Backend.hpp"
#include "database/PostgreSQLBackend.hpp"
#include "database/CitusBackend.hpp"
#include "logging/Logger.hpp"

std::unique_ptr<DatabaseBackend> DatabaseBackend::CreateBackend(const std::string& type) {
    if (type == "postgresql" || type == "postgres") {
        Logger::Info("Creating PostgreSQL backend");
        return std::make_unique<PostgreSQLBackend>();
    }
#ifdef USE_CITUS
    else if (type == "citus") {
        Logger::Info("Creating Citus backend");
        return std::make_unique<CitusBackend>();
    }
#endif
    else {
        Logger::Error("Unknown database backend type: {}", type);
        throw std::runtime_error("Unknown database backend type: " + type);
    }
}
