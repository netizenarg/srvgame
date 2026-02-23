#pragma once

#include <mutex>
#include <shared_mutex>
#include <string>

#include <nlohmann/json.hpp>
#include <Python.h>


// Python GIL helper
class PyGILGuard {
public:
    PyGILGuard() : state_(PyGILState_Ensure()) {}
    ~PyGILGuard() { PyGILState_Release(state_); }

private:
    PyGILState_STATE state_;
};

class PythonModule {
public:
    PythonModule(const std::string& moduleName, const std::string& filePath);
    ~PythonModule();

    // Disable copy
    PythonModule(const PythonModule&) = delete;
    PythonModule& operator=(const PythonModule&) = delete;

    // Enable move
    PythonModule(PythonModule&& other) noexcept;
    PythonModule& operator=(PythonModule&& other) noexcept;

    // Module operations
    bool Load();
    bool Reload();
    void Unload();

    // Function calling
    bool CallFunction(const std::string& funcName, const nlohmann::json& args = {});
    nlohmann::json CallFunctionWithResult(const std::string& funcName,
                                         const nlohmann::json& args = {});

    // Module info
    bool HasFunction(const std::string& funcName) const;
    std::string GetName() const { return moduleName_; }
    std::string GetFilePath() const { return filePath_; }
    bool IsLoaded() const { return module_ != nullptr; }
    std::string GetLastError() const;

    // Utility
    std::vector<std::string> GetFunctionNames() const;
    nlohmann::json GetModuleInfo() const;

private:
    std::string moduleName_;
    std::string filePath_;
    PyObject* module_;
    mutable std::mutex mutex_;
    std::string lastError_;

    // Helper methods
    PyObject* CreatePyArgs(const nlohmann::json& args);
    nlohmann::json PyObjectToJson(PyObject* obj);
    PyObject* JsonToPyObject(const nlohmann::json& json);
    std::string PyObjectToString(PyObject* obj);

    // Error handling
    void SetError(const std::string& error);
    void ClearError();
    bool CheckPythonError();

    // Reference counting helpers
    void AcquireModule();
    void ReleaseModule();
};
