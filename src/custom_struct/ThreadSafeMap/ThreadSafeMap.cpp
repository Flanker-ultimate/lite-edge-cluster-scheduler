//
// Created by lxsa1 on 18/10/2024.
//

#include "ThreadSafeMap.h"
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>


template<typename K, typename V>
void ThreadSafeMap<K, V>::insert(const K &key, const V &value){
    std::unique_lock<std::shared_mutex> lock(mtx);
    data.insert({key, value});
}

template<typename K, typename V>
bool ThreadSafeMap<K, V>::set(const K &key, const V &value){
    std::unique_lock<std::shared_mutex> lock(mtx);
    auto [it, inserted] = data.insert_or_assign(key, value);
    return inserted;
}

template<typename K, typename V>
std::optional<const V> ThreadSafeMap<K, V>::get(const K& key) const {
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto it = data.find(key);
    if (it == data.end()) {
        return std::nullopt; // Return an empty optional
    }
    return it->second; // Return a reference to the value
}

template<typename K, typename V>
bool ThreadSafeMap<K, V>::contains(const K &key) const {
    std::shared_lock<std::shared_mutex> lock(mtx);
    return data.count(key) > 0;
}

template<typename K, typename V>
size_t ThreadSafeMap<K, V>::size() const {
    std::shared_lock<std::shared_mutex> lock(mtx);
    return data.size();
}

template<typename K, typename V>
void ThreadSafeMap<K, V>::clear() {
    std::unique_lock<std::shared_mutex> lock(mtx);
    data.clear();
}


