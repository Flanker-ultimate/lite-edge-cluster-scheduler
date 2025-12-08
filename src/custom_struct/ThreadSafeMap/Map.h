//
// Created by lxsa1 on 18/10/2024.
//
#ifndef MAP_H
#define MAP_H
#include <map>
#include <optional>
#include <stdexcept>
template <typename K, typename V>
class Map {
public:
    virtual ~Map() = default;
    virtual void insert(const K& key, const V& value) = 0;
    virtual bool set(const K& key, const V& value) = 0;
    virtual std::optional<const V>  get(const K& key) const = 0;

    virtual bool contains(const K& key) const = 0;
    virtual size_t size() const = 0;
    virtual void clear() = 0;
};


#endif //MAP_H
