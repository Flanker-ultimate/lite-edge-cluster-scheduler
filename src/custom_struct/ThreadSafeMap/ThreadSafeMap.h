//
// Created by lxsa1 on 18/10/2024.
//
#ifndef THREADSAFEMAP_H
#define THREADSAFEMAP_H

#include "Map.h"
#include <shared_mutex>
#include <unordered_map>

template <typename K, typename V>
class ThreadSafeMap : public Map<K, V> {
public:
    void insert(const K& key, const V& value) override;
    bool set(const K& key, const V& value) override;
    std::optional<const V>  get(const K& key) const override;
    bool contains(const K& key) const override;
    size_t size() const override;
    void clear() override;
private:
    std::unordered_map<K, V> data;
    mutable std::shared_mutex mtx; // mutable allows use in const methods

};

#endif //THREADSAFEMAP_H
