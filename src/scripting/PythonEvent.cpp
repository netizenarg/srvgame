#include <chrono>
#include <thread>
#include <queue>
#include <algorithm>

#include "scripting/PythonEvent.hpp"

namespace PythonScripting {

// =============== PythonEventHandler Implementation ===============

PythonEventHandler::PythonEventHandler(const std::string& name,
                                     const std::string& moduleName,
                                     const std::string& functionName,
                                     int priority)
    : name_(name), moduleName_(moduleName), functionName_(functionName), priority_(priority) {
}

bool PythonEventHandler::HandleEvent(const nlohmann::json& eventData) {
    // This would call Python via the scripting engine
    // For now, it's a stub
    auto& scripting = PythonScripting::GetInstance();
    return scripting.CallFunction(moduleName_, functionName_, eventData);
}

bool PythonEventHandler::IsValid() const {
    auto& scripting = PythonScripting::GetInstance();
    std::vector<std::string> modules = scripting.GetLoadedModules();
    return std::find(modules.begin(), modules.end(), moduleName_) != modules.end();
}

// =============== EventDispatcher Implementation ===============

EventDispatcher& EventDispatcher::GetInstance() {
    static EventDispatcher instance;
    return instance;
}

void EventDispatcher::RegisterHandler(const std::string& eventName,
                                     std::shared_ptr<IEventHandler> handler) {
    std::unique_lock<std::shared_mutex> lock(handlersMutex_);
    
    // Insert handler in priority order
    auto& handlers = handlers_[eventName];
    auto it = std::lower_bound(handlers.begin(), handlers.end(), handler,
        [](const std::shared_ptr<IEventHandler>& a,
           const std::shared_ptr<IEventHandler>& b) {
            return a->GetPriority() > b->GetPriority();
        });
    
    handlers.insert(it, handler);
}

void EventDispatcher::UnregisterHandler(const std::string& eventName,
                                       const std::string& handlerName) {
    std::unique_lock<std::shared_mutex> lock(handlersMutex_);
    
    auto it = handlers_.find(eventName);
    if (it != handlers_.end()) {
        auto& handlers = it->second;
        handlers.erase(
            std::remove_if(handlers.begin(), handlers.end(),
                [&handlerName](const std::shared_ptr<IEventHandler>& handler) {
                    return handler->GetName() == handlerName;
                }),
            handlers.end()
        );
        
        if (handlers.empty()) {
            handlers_.erase(it);
        }
    }
}

bool EventDispatcher::DispatchEvent(const std::string& eventName,
                                   const nlohmann::json& eventData) {
    std::shared_lock<std::shared_mutex> lock(handlersMutex_);
    
    auto it = handlers_.find(eventName);
    if (it == handlers_.end()) {
        return false;
    }
    
    bool anySuccess = false;
    for (const auto& handler : it->second) {
        if (handler->HandleEvent(eventData)) {
            anySuccess = true;
        }
    }
    
    return anySuccess;
}

std::vector<std::string> EventDispatcher::GetRegisteredEvents() const {
    std::shared_lock<std::shared_mutex> lock(handlersMutex_);
    
    std::vector<std::string> events;
    for (const auto& [eventName, handlers] : handlers_) {
        events.push_back(eventName);
    }
    
    return events;
}

std::vector<std::string> EventDispatcher::GetHandlersForEvent(const std::string& eventName) const {
    std::shared_lock<std::shared_mutex> lock(handlersMutex_);
    
    auto it = handlers_.find(eventName);
    if (it == handlers_.end()) {
        return {};
    }
    
    std::vector<std::string> handlerNames;
    for (const auto& handler : it->second) {
        handlerNames.push_back(handler->GetName());
    }
    
    return handlerNames;
}

// =============== EventQueue Implementation ===============

EventQueue::EventQueue(size_t maxSize)
    : maxSize_(maxSize), running_(false) {
}

EventQueue::~EventQueue() {
    StopProcessing();
}

bool EventQueue::PushEvent(const std::string& eventName,
                          const nlohmann::json& eventData,
                          int priority) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    if (queue_.size() >= maxSize_) {
        return false;
    }
    
    QueuedEvent event{
        eventName,
        eventData,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count(),
        priority
    };
    
    queue_.push(std::move(event));
    queueCV_.notify_one();
    
    return true;
}

bool EventQueue::PopEvent(QueuedEvent& event) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    if (queue_.empty()) {
        return false;
    }
    
    event = queue_.top();
    queue_.pop();
    return true;
}

size_t EventQueue::Size() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return queue_.size();
}

size_t EventQueue::Capacity() const {
    return maxSize_;
}

void EventQueue::Clear() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
}

void EventQueue::StartProcessing() {
    if (running_) {
        return;
    }
    
    running_ = true;
    processThread_ = std::thread(&EventQueue::ProcessLoop, this);
}

void EventQueue::StopProcessing() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    queueCV_.notify_all();
    
    if (processThread_.joinable()) {
        processThread_.join();
    }
}

void EventQueue::ProcessLoop() {
    while (running_) {
        QueuedEvent event;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (queue_.empty()) {
                queueCV_.wait(lock, [this]() {
                    return !queue_.empty() || !running_;
                });
            }
            
            if (!running_) {
                break;
            }
            
            if (queue_.empty()) {
                continue;
            }
            
            event = queue_.top();
            queue_.pop();
        }
        
        // Dispatch the event
        auto& dispatcher = EventDispatcher::GetInstance();
        dispatcher.DispatchEvent(event.name, event.data);
    }
}

// =============== ScheduledEvent Implementation ===============

ScheduledEvent::ScheduledEvent(const std::string& eventName,
                             const nlohmann::json& eventData,
                             int64_t executeAt,
                             bool repeat,
                             int64_t interval)
    : eventName_(eventName),
      eventData_(eventData),
      executeAt_(executeAt),
      repeat_(repeat),
      interval_(interval) {
}

bool ScheduledEvent::ShouldExecute() const {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return now >= executeAt_;
}

bool ScheduledEvent::Execute() {
    auto& dispatcher = EventDispatcher::GetInstance();
    return dispatcher.DispatchEvent(eventName_, eventData_);
}

void ScheduledEvent::Reschedule() {
    if (repeat_) {
        executeAt_ += interval_;
    }
}

// =============== EventScheduler Implementation ===============

EventScheduler& EventScheduler::GetInstance() {
    static EventScheduler instance;
    return instance;
}

EventScheduler::EventScheduler() : running_(false) {
    StartProcessing();
}

EventScheduler::~EventScheduler() {
    StopProcessing();
}

void EventScheduler::ScheduleEvent(const std::string& eventName,
                                 const nlohmann::json& eventData,
                                 int64_t delayMs,
                                 bool repeat,
                                 int64_t intervalMs) {
    int64_t executeAt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + delayMs;
    
    std::lock_guard<std::mutex> lock(scheduledEventsMutex_);
    scheduledEvents_.push_back(
        std::make_unique<ScheduledEvent>(eventName, eventData, executeAt, repeat, intervalMs)
    );
}

void EventScheduler::CancelEvent(const std::string& eventName) {
    std::lock_guard<std::mutex> lock(scheduledEventsMutex_);
    
    scheduledEvents_.erase(
        std::remove_if(scheduledEvents_.begin(), scheduledEvents_.end(),
            [&eventName](const std::unique_ptr<ScheduledEvent>& event) {
                return event->GetName() == eventName;
            }),
        scheduledEvents_.end()
    );
}

void EventScheduler::Update() {
    ProcessScheduledEvents();
}

void EventScheduler::StartProcessing() {
    if (running_) {
        return;
    }
    
    running_ = true;
    schedulerThread_ = std::thread(&EventScheduler::ProcessScheduledEvents, this);
}

void EventScheduler::StopProcessing() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (schedulerThread_.joinable()) {
        schedulerThread_.join();
    }
}

void EventScheduler::ProcessScheduledEvents() {
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(scheduledEventsMutex_);
            
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            for (auto& event : scheduledEvents_) {
                if (event->ShouldExecute()) {
                    event->Execute();
                    
                    if (event->repeat_) {
                        event->Reschedule();
                    } else {
                        // Mark for removal
                        event.reset();
                    }
                }
            }
            
            // Remove executed non-repeating events
            scheduledEvents_.erase(
                std::remove_if(scheduledEvents_.begin(), scheduledEvents_.end(),
                    [](const std::unique_ptr<ScheduledEvent>& event) {
                        return !event;
                    }),
                scheduledEvents_.end()
            );
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace PythonScripting