#include <filesystem>
#include "scripting/PythonModule.hpp"


PythonModule::PythonModule(const std::string& moduleName, const std::string& filePath)
    : moduleName_(moduleName), filePath_(filePath), module_(nullptr) {
}

PythonModule::~PythonModule() {
    Unload();
}

bool PythonModule::Load() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (module_) {
        SetError("Module already loaded");
        return false;
    }

    PyGILGuard gil;

    // Add directory to Python path
    std::filesystem::path path(filePath_);
    std::string dir = path.parent_path().string();

    std::string code = "import sys\n";
    code += "if '" + dir + "' not in sys.path:\n";
    code += "    sys.path.insert(0, '" + dir + "')\n";

    PyRun_SimpleString(code.c_str());

    // Load the module
    PyObject* pName = PyUnicode_FromString(moduleName_.c_str());
    if (!pName) {
        SetError("Failed to create module name");
        return false;
    }

    module_ = PyImport_Import(pName);
    Py_DECREF(pName);

    if (!module_) {
        CheckPythonError();
        return false;
    }

    return true;
}

bool PythonModule::Reload() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!module_) {
        SetError("Module not loaded");
        return false;
    }

    PyGILGuard gil;

    PyObject* reloaded = PyImport_ReloadModule(module_);
    if (!reloaded) {
        CheckPythonError();
        return false;
    }

    // Swap references
    Py_DECREF(module_);
    module_ = reloaded;

    return true;
}

void PythonModule::Unload() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (module_) {
        PyGILGuard gil;
        Py_DECREF(module_);
        module_ = nullptr;
    }
}

bool PythonModule::CallFunction(const std::string& funcName, const nlohmann::json& args) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!module_) {
        SetError("Module not loaded");
        return false;
    }

    PyGILGuard gil;

    // Get the function
    PyObject* pFunc = PyObject_GetAttrString(module_, funcName.c_str());
    if (!pFunc || !PyCallable_Check(pFunc)) {
        if (pFunc) Py_DECREF(pFunc);
        SetError("Function not found or not callable: " + funcName);
        return false;
    }

    // Create arguments
    PyObject* pArgs = CreatePyArgs(args);

    // Call the function
    PyObject* pResult = PyObject_CallObject(pFunc, pArgs);

    // Cleanup
    Py_XDECREF(pArgs);
    Py_DECREF(pFunc);

    if (!pResult) {
        CheckPythonError();
        return false;
    }

    Py_DECREF(pResult);
    return true;
}

nlohmann::json PythonModule::CallFunctionWithResult(const std::string& funcName,
                                                   const nlohmann::json& args) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!module_) {
        SetError("Module not loaded");
        return nlohmann::json();
    }

    PyGILGuard gil;

    // Get the function
    PyObject* pFunc = PyObject_GetAttrString(module_, funcName.c_str());
    if (!pFunc || !PyCallable_Check(pFunc)) {
        if (pFunc) Py_DECREF(pFunc);
        SetError("Function not found or not callable: " + funcName);
        return nlohmann::json();
    }

    // Create arguments
    PyObject* pArgs = CreatePyArgs(args);

    // Call the function
    PyObject* pResult = PyObject_CallObject(pFunc, pArgs);

    // Cleanup
    Py_XDECREF(pArgs);
    Py_DECREF(pFunc);

    if (!pResult) {
        CheckPythonError();
        return nlohmann::json();
    }

    // Convert result to JSON
    nlohmann::json result = PyObjectToJson(pResult);
    Py_DECREF(pResult);

    return result;
}

bool PythonModule::HasFunction(const std::string& funcName) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!module_) {
        return false;
    }

    PyGILGuard gil;

    PyObject* pFunc = PyObject_GetAttrString(module_, funcName.c_str());
    if (!pFunc) {
        PyErr_Clear();
        return false;
    }

    bool isCallable = PyCallable_Check(pFunc) != 0;
    Py_DECREF(pFunc);

    return isCallable;
}

std::vector<std::string> PythonModule::GetFunctionNames() const {
    std::vector<std::string> functions;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!module_) {
        return functions;
    }

    PyGILGuard gil;

    PyObject* dict = PyModule_GetDict(module_);
    if (!dict) {
        return functions;
    }

    PyObject* key, *value;
    Py_ssize_t pos = 0;

    while (PyDict_Next(dict, &pos, &key, &value)) {
        if (PyCallable_Check(value)) {
            PyObject* strKey = PyObject_Str(key);
            if (strKey) {
                PyObject* utf8 = PyUnicode_AsUTF8String(strKey);
                if (utf8) {
                    const char* funcName = PyBytes_AsString(utf8);
                    functions.push_back(funcName);
                    Py_DECREF(utf8);
                }
                Py_DECREF(strKey);
            }
        }
    }

    return functions;
}

nlohmann::json PythonModule::GetModuleInfo() const {
    nlohmann::json info;

    std::lock_guard<std::mutex> lock(mutex_);

    info["name"] = moduleName_;
    info["file_path"] = filePath_;
    info["loaded"] = (module_ != nullptr);

    if (module_) {
        info["functions"] = GetFunctionNames();
    }

    return info;
}

// Helper methods
PyObject* PythonModule::CreatePyArgs(const nlohmann::json& args) {
    // Similar implementation to PythonScripting.cpp
    if (args.is_null() || args.empty()) {
        return PyTuple_New(0);
    }

    if (args.is_array()) {
        PyObject* tuple = PyTuple_New(args.size());
        for (size_t i = 0; i < args.size(); ++i) {
            PyObject* item = JsonToPyObject(args[i]);
            PyTuple_SetItem(tuple, i, item);
        }
        return tuple;
    }

    // Single argument
    PyObject* pValue = JsonToPyObject(args);
    PyObject* tuple = PyTuple_New(1);
    PyTuple_SetItem(tuple, 0, pValue);
    return tuple;
}

nlohmann::json PythonModule::PyObjectToJson(PyObject* obj) {
    // Similar implementation to PythonScripting.cpp
    if (!obj || obj == Py_None) {
        return nlohmann::json();
    }

    if (PyBool_Check(obj)) {
        return nlohmann::json(obj == Py_True);
    }

    if (PyLong_Check(obj)) {
        return nlohmann::json(PyLong_AsLong(obj));
    }

    if (PyFloat_Check(obj)) {
        return nlohmann::json(PyFloat_AsDouble(obj));
    }

    if (PyUnicode_Check(obj)) {
        PyObject* bytes = PyUnicode_AsUTF8String(obj);
        const char* str = PyBytes_AsString(bytes);
        nlohmann::json result = str;
        Py_DECREF(bytes);
        return result;
    }

    if (PyList_Check(obj)) {
        nlohmann::json::array_t array;
        Py_ssize_t size = PyList_Size(obj);
        for (Py_ssize_t i = 0; i < size; ++i) {
            PyObject* item = PyList_GetItem(obj, i);
            array.push_back(PyObjectToJson(item));
        }
        return array;
    }

    if (PyDict_Check(obj)) {
        nlohmann::json object;
        PyObject* key, *value;
        Py_ssize_t pos = 0;

        while (PyDict_Next(obj, &pos, &key, &value)) {
            std::string keyStr;
            if (PyUnicode_Check(key)) {
                PyObject* bytes = PyUnicode_AsUTF8String(key);
                keyStr = PyBytes_AsString(bytes);
                Py_DECREF(bytes);
            }
            object[keyStr] = PyObjectToJson(value);
        }
        return object;
    }

    return nlohmann::json();
}

PyObject* PythonModule::JsonToPyObject(const nlohmann::json& json) {
    // Similar implementation to PythonScripting.cpp
    if (json.is_null()) {
        Py_RETURN_NONE;
    }

    if (json.is_boolean()) {
        return PyBool_FromLong(json.get<bool>());
    }

    if (json.is_number_integer()) {
        return PyLong_FromLong(json.get<long>());
    }

    if (json.is_number_float()) {
        return PyFloat_FromDouble(json.get<double>());
    }

    if (json.is_string()) {
        return PyUnicode_FromString(json.get<std::string>().c_str());
    }

    if (json.is_array()) {
        PyObject* list = PyList_New(json.size());
        for (size_t i = 0; i < json.size(); ++i) {
            PyObject* item = JsonToPyObject(json[i]);
            PyList_SET_ITEM(list, i, item);
        }
        return list;
    }

    if (json.is_object()) {
        PyObject* dict = PyDict_New();
        for (const auto& [key, value] : json.items()) {
            PyObject* pyValue = JsonToPyObject(value);
            PyDict_SetItemString(dict, key.c_str(), pyValue);
            Py_DECREF(pyValue);
        }
        return dict;
    }

    return nullptr;
}

std::string PythonModule::PyObjectToString(PyObject* obj) {
    if (!obj) {
        return "";
    }

    PyObject* strObj = PyObject_Str(obj);
    if (!strObj) {
        PyErr_Clear();
        return "";
    }

    PyObject* utf8 = PyUnicode_AsUTF8String(strObj);
    if (!utf8) {
        Py_DECREF(strObj);
        PyErr_Clear();
        return "";
    }

    const char* str = PyBytes_AsString(utf8);
    std::string result = str ? str : "";

    Py_DECREF(utf8);
    Py_DECREF(strObj);

    return result;
}

void PythonModule::SetError(const std::string& error) {
    lastError_ = error;
}

void PythonModule::ClearError() {
    lastError_.clear();
}

bool PythonModule::CheckPythonError() {
    if (PyErr_Occurred()) {
        PyObject* type, *value, *traceback;
        PyErr_Fetch(&type, &value, &traceback);

        if (value) {
            PyObject* strValue = PyObject_Str(value);
            if (strValue) {
                PyObject* utf8 = PyUnicode_AsUTF8String(strValue);
                if (utf8) {
                    lastError_ = PyBytes_AsString(utf8);
                    Py_DECREF(utf8);
                }
                Py_DECREF(strValue);
            }
        }

        PyErr_Clear();
        if (type) Py_DECREF(type);
        if (value) Py_DECREF(value);
        if (traceback) Py_DECREF(traceback);

        return true;
    }

    return false;
}

std::string PythonModule::GetLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}
