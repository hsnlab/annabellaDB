//  Copyright 2019 U.C. Berkeley RISE Lab
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef INCLUDE_LATTICES_CORE_LATTICES_HPP_
#define INCLUDE_LATTICES_CORE_LATTICES_HPP_

#include "lattice.hpp"
#include "types.hpp"

class BoolLattice : public Lattice<bool> {
 protected:
  void do_merge(const bool &e) { element |= e; }

 public:
  BoolLattice() : Lattice(false) {}
  BoolLattice(const bool &e) : Lattice(e) {}
};

template <typename T>
class MaxLattice : public Lattice<T> {
 protected:
  void do_merge(const T &e) {
    int current = this->element;

    if (current < e) {
      this->element = e;
    }
  }

 public:
  MaxLattice() : Lattice<T>(T()) {}
  MaxLattice(const T &e) : Lattice<T>(e) {}

  // for now, all non-merge methods are non-destructive
  MaxLattice<T> add(T n) const { return MaxLattice<T>(this->element + n); }

  MaxLattice<T> subtract(T n) const { return MaxLattice<T>(this->element - n); }
};

template <typename T>
class SetLattice : public Lattice<set<T>> {
 protected:
  void do_merge(const set<T> &e) {
    for (const T &elem : e) {
      this->element.insert(elem);
    }
  }

 public:
  SetLattice() : Lattice<set<T>>(set<T>()) {}

  SetLattice(const set<T> &e) : Lattice<set<T>>(e) {}

  MaxLattice<unsigned> size() const { return this->element.size(); }

  void insert(T e) { this->element.insert(std::move(e)); }

  SetLattice<T> intersect(set<T> s) const {
    set<T> res;

    for (const T &that_elem : s) {
      for (const T &this_elem : this->element) {
        if (this_elem == that_elem) res.insert(this_elem);
      }
    }

    return SetLattice<T>(res);
  }

  SetLattice<T> project(bool (*f)(T)) const {
    set<T> res;

    for (const T &elem : this->element) {
      if (f(elem)) res.insert(elem);
    }

    return SetLattice<T>(res);
  }
};

template <typename T>
class OrderedSetLattice : public Lattice<ordered_set<T>> {
 protected:
  void do_merge(const ordered_set<T> &e) {
    for (const T &elem : e) {
      this->element.insert(elem);
    }
  }

 public:
  OrderedSetLattice() : Lattice<ordered_set<T>>(ordered_set<T>()) {}

  OrderedSetLattice(const ordered_set<T> &e) : Lattice<ordered_set<T>>(e) {}

  MaxLattice<unsigned> size() const { return this->element.size(); }

  void insert(T e) { this->element.insert(std::move(e)); }

  OrderedSetLattice<T> intersect(ordered_set<T> s) const {
    ordered_set<T> res;

    for (const T &that_elem : s) {
      for (const T &this_elem : this->element) {
        if (this_elem == that_elem) res.insert(this_elem);
      }
    }

    return OrderedSetLattice<T>(res);
  }

  OrderedSetLattice<T> project(bool (*f)(T)) const {
    ordered_set<T> res;

    for (const T &elem : this->element) {
      if (f(elem)) res.insert(elem);
    }

    return OrderedSetLattice<T>(res);
  }
};

template <typename K, typename V>
class MapLattice : public Lattice<map<K, V>> {
 protected:
  void insert_pair(const K &k, const V &v) {
    auto search = this->element.find(k);
    if (search != this->element.end()) {
      static_cast<V *>(&(search->second))->merge(v);
    } else {
      // need to copy v since we will be "growing" it within the lattice
      V new_v = v;
      this->element.emplace(k, new_v);
    }
  }

  void do_merge(const map<K, V> &m) {
    for (const auto &pair : m) {
      this->insert_pair(pair.first, pair.second);
    }
  }

 public:
  MapLattice() : Lattice<map<K, V>>(map<K, V>()) {}
  MapLattice(const map<K, V> &m) : Lattice<map<K, V>>(m) {}
  MaxLattice<unsigned> size() const { return this->element.size(); }

  MapLattice<K, V> intersect(MapLattice<K, V> other) const {
    MapLattice<K, V> res;
    map<K, V> m = other.reveal();

    for (const auto &pair : m) {
      if (this->contains(pair.first).reveal()) {
        res.insert_pair(pair.first, this->at(pair.first));
        res.insert_pair(pair.first, pair.second);
      }
    }

    return res;
  }

  MapLattice<K, V> project(bool (*f)(V)) const {
    map<K, V> res;
    for (const auto &pair : this->element) {
      if (f(pair.second)) res.emplace(pair.first, pair.second);
    }
    return MapLattice<K, V>(res);
  }

  BoolLattice contains(K k) const {
    auto it = this->element.find(k);
    if (it == this->element.end())
      return BoolLattice(false);
    else
      return BoolLattice(true);
  }

  SetLattice<K> key_set() const {
    set<K> res;
    for (const auto &pair : this->element) {
      res.insert(pair.first);
    }
    return SetLattice<K>(res);
  }

  V &at(K k) { return this->element[k]; }

  void remove(K k) {
    auto it = this->element.find(k);
    if (it != this->element.end()) this->element.erase(k);
  }

  void insert(const K &k, const V &v) { this->insert_pair(k, v); }
};

#endif  // INCLUDE_LATTICES_CORE_LATTICES_HPP_
