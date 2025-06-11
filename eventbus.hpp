#pragma once

#ifndef __EVENTBUS_HPP__
#define __EVENTBUS_HPP__

#include <algorithm>
#include <any>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <memory>

class Property final
{
public:
    Property() noexcept = default;

    template<typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Property>>>
    Property(T&& value) : _value(std::forward<T>(value))
    {
        static_assert(std::is_copy_constructible_v<std::decay_t<T>> || std::is_move_constructible_v<std::decay_t<T>>,
                      "Stored type must be copy or move constructible");
    }

    Property(const Property& other) = default;

    Property(Property&& other) noexcept = default;

    Property& operator=(const Property& other) = default;

    Property& operator=(Property&& other) noexcept = default;

    template<typename T>
    Property& operator=(T&& value)
    {
        _value = std::forward<T>(value);
        return *this;
    }

    bool has_value() const noexcept
    {
        return _value.has_value();
    }

    const std::type_info& type() const noexcept
    {
        return _value.type();
    }

    // 获取值（左值引用）
    template<typename T>
    T& get_ref()
    {
        using U = std::decay_t<T>;
        if (!_value.has_value())
        {
            throw std::bad_any_cast();
        }
        try
        {
            return std::any_cast<U&>(_value);
        }
        catch (const std::bad_any_cast& e)
        {
            std::cerr << "Type mismatch! Requested: " << typeid(U).name() << ", Stored: " << _value.type().name()
                      << std::endl;
            throw e;
        }
    }

    // 获取值（const左值引用）
    template<typename T>
    const T& get_cref() const
    {
        using U = std::decay_t<T>;
        if (!_value.has_value())
        {
            throw std::bad_any_cast();
        }
        try
        {
            return std::any_cast<const U&>(_value);
        }
        catch (const std::bad_any_cast& e)
        {
            std::cerr << "Type mismatch! Requested: " << typeid(U).name() << ", Stored: " << _value.type().name()
                      << std::endl;
            throw e;
        }
    }

    // 安全获取值（拷贝）
    template<typename T>
    T get_value() const
    {
        using U = std::decay_t<T>;
        static_assert(std::is_copy_constructible_v<U>, "Cannot get non-copyable type by value");
        if (!_value.has_value())
        {
            throw std::bad_any_cast();
        }
        try
        {
            return std::any_cast<const U&>(_value);
        }
        catch (const std::bad_any_cast& e)
        {
            std::cerr << "Type mismatch! Requested: " << typeid(U).name() << ", Stored: " << _value.type().name()
                      << std::endl;
            throw e;
        }
    }

    // 提取值（移动语义，使用后对象置空）
    template<typename T>
    T extract()
    {
        using U = std::decay_t<T>;
        static_assert(std::is_move_constructible_v<U>, "Cannot extract non-movable type");
        if (!_value.has_value())
        {
            throw std::bad_any_cast();
        }
        try
        {
            U value = std::move(std::any_cast<U&>(_value));
            _value.reset();
            return value;
        }
        catch (const std::bad_any_cast& e)
        {
            std::cerr << "Type mismatch! Requested: " << typeid(U).name() << ", Stored: " << _value.type().name()
                      << std::endl;
            throw e;
        }
    }

    void reset() noexcept
    {
        _value.reset();
    }

    void swap(Property& other) noexcept
    {
        _value.swap(other._value);
    }

private:
    std::any _value;
};

using PropertyMap = std::unordered_map<std::string, Property>;

class EventBus final
{
public:
    using CallBack = std::function<void(const PropertyMap& props)>;
    using CallBackSPtr = std::shared_ptr<CallBack>;
    using CallBackWPtr = std::weak_ptr<CallBack>;
    using CallBackWList = std::vector<CallBackWPtr>;

    static EventBus& GetInstance()
    {
        static EventBus eventBus;
        return eventBus;
    }

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;
    ~EventBus() = default;

    void notify(const std::string& topic, const PropertyMap& props)
    {
        // 更新最新数据
        {
            std::unique_lock<std::shared_mutex> lock(_dataMutex);
            _latestData[topic] = props;
        }

        CallBackWList callbacks;
        {
            std::shared_lock<std::shared_mutex> read_lock(_globalMutex);
            auto it = _topics.find(topic);
            if (it == _topics.end())
            {
                return;
            }
            std::lock_guard<std::mutex> lock(it->second.mutex);
            callbacks = it->second.callbacks;
        }

        for (auto& cb : callbacks)
        {
            if (auto callback = cb.lock())
            {
                try
                {
                    (void)std::async(std::launch::async, *callback, props);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "EventBus exception: " << e.what() << std::endl;
                }
                catch (...)
                {
                    std::cerr << "Unknown EventBus exception" << std::endl;
                }
            }
        }
    }

    bool getLatest(const std::string& topic, PropertyMap& data) const
    {
        std::shared_lock<std::shared_mutex> lock(_dataMutex);
        auto it = _latestData.find(topic);
        if (it != _latestData.end())
        {
            data = it->second;
            return true;
        }
        return false;
    }

    std::string listen(const std::string& topic, CallBack func)
    {
        auto callback_ptr = std::make_shared<CallBack>(std::move(func));
        std::string id = generateId();
        std::unique_lock<std::shared_mutex> write_lock(_globalMutex);
        auto& topic_data = _topics[topic];
        write_lock.unlock();
        std::lock_guard<std::mutex> lock(topic_data.mutex);
        topic_data.callbacks.emplace_back(callback_ptr);
        topic_data.callbackMap[id] = callback_ptr;
        return id;
    }

    void unlisten(const std::string& topic, const std::string& id)
    {
        std::shared_lock<std::shared_mutex> read_lock(_globalMutex);
        auto it = _topics.find(topic);
        if (it == _topics.end())
        {
            return;
        }
        auto& topic_data = it->second;
        std::lock_guard<std::mutex> lock(topic_data.mutex);
        topic_data.callbackMap.erase(id);
        // 惰性清理
        auto& callbacks = topic_data.callbacks;
        callbacks.erase(
            std::remove_if(callbacks.begin(), callbacks.end(), [](const CallBackWPtr& wp) { return wp.expired(); }),
            callbacks.end());
    }

private:
    struct TopicData
    {
        std::mutex mutex;
        CallBackWList callbacks;
        std::unordered_map<std::string, CallBackSPtr> callbackMap;
    };

    std::string generateId()
    {
        return std::to_string(_idCounter.fetch_add(1, std::memory_order_relaxed));
    }

    mutable std::shared_mutex _globalMutex;
    mutable std::shared_mutex _dataMutex;
    std::atomic<size_t> _idCounter{0};
    std::unordered_map<std::string, TopicData> _topics;
    std::unordered_map<std::string, PropertyMap> _latestData;

    EventBus() = default;
};

#endif //!__EVENTBUS_HPP__
