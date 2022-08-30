#ifndef OLDSTL_CONTAINERS_H
#define OLDSTL_CONTAINERS_H

#ifndef OLDSTL_BINDINGS
  #error \
      "This file contains definitions for OLDSTL containers.  If you wanted a gc'd container build, include gc_containers.h"
#endif

#include <assert.h>
#include <ctype.h>   // isalpha(), isdigit()
#include <stdlib.h>  // malloc
#include <string.h>  // strlen

#include <algorithm>  // sort() is templated
// https://stackoverflow.com/questions/3882346/forward-declare-file
#include <climits>  // CHAR_BIT
#include <cstdint>
#include <cstdio>  // FILE*
#include <initializer_list>
#include <vector>

#include "common.h"

#ifdef DUMB_ALLOC
  #include "cpp/dumb_alloc.h"
  #define malloc dumb_malloc
  #define free dumb_free
#endif

#define GLOBAL_STR(name, val) Str* name = StrFromC(val, sizeof(val) - 1)
#define GLOBAL_LIST(T, N, name, array) List<T>* name = NewList<T>(array);

class Obj;

struct Heap {
  void Init(int byte_count) {
  }

  void Bump() {
  }

  void Collect() {
  }

  void* Allocate(int num_bytes) {
    return calloc(num_bytes, 1);
  }

  void PushRoot(Obj** p) {
  }

  void PopRoot() {
  }
};

extern Heap gHeap;

// clang-format off
#include "mycpp/gc_tag.h"
#include "mycpp/gc_obj.h"
#include "mycpp/gc_alloc.h"
// clang-format on

struct StackRoots {
  StackRoots(std::initializer_list<void*> roots) {
  }
};

template <class K, class V>
class Dict;

template <class K, class V>
class DictIter;

// clang-format off
#include "mycpp/tuple_types.h"
#include "mycpp/error_types.h"
#include "mycpp/gc_str.h"
#include "mycpp/oldstl_mylib.h" // mylib namespace
// clang-format on

extern Str* kEmptyString;

//
// Data Types
//

#include "mycpp/gc_slab.h"
#include "mycpp/gc_list.h"
#include "mycpp/gc_list_iter.h"


// TODO: A proper dict index should get rid of this unusual sentinel scheme.
// The index can be -1 on deletion, regardless of the type of the key.

template <class K, class V>
void dict_next(DictIter<K, V>* it, const std::vector<std::pair<K, V>>& items) {
  ++it->i_;
}

template <class V>
void dict_next(DictIter<Str*, V>* it,
               const std::vector<std::pair<Str*, V>>& items) {
  while (true) {
    ++it->i_;
    if (it->Done()) {
      break;
    }
    if (items[it->i_].first) {  // not nullptr
      break;
    }
  }
}

template <class K, class V>
bool dict_done(DictIter<K, V>* it, const std::vector<std::pair<K, V>>& items) {
  int n = items.size();
  return it->i_ >= n;
}

template <class V>
bool dict_done(DictIter<Str*, V>* it,
               const std::vector<std::pair<Str*, V>>& items) {
  int n = items.size();
  if (it->i_ >= n) {
    return true;
  }
  for (int j = it->i_; j < n; ++j) {
    if (items[j].first) {  // there's still something left
      return false;
    }
  }
  return true;
}

template <class K, class V>
class DictIter {
 public:
  explicit DictIter(Dict<K, V>* D) : D_(D), i_(0) {
  }
  void Next() {
    dict_next(this, D_->items_);
  }
  bool Done() {
    return dict_done(this, D_->items_);
  }
  K Key() {
    return D_->items_[i_].first;
  }
  V Value() {
    return D_->items_[i_].second;
  }

  Dict<K, V>* D_;
  int i_;
};

bool str_equals(Str* left, Str* right);

// Specialized functions
template <class V>
int find_by_key(const std::vector<std::pair<Str*, V>>& items, Str* key) {
  int n = items.size();
  for (int i = 0; i < n; ++i) {
    Str* s = items[i].first;  // nullptr for deleted entries
    if (s && str_equals(s, key)) {
      return i;
    }
  }
  return -1;
}

template <class V>
int find_by_key(const std::vector<std::pair<int, V>>& items, int key) {
  int n = items.size();
  for (int i = 0; i < n; ++i) {
    if (items[i].first == key) {
      return i;
    }
  }
  return -1;
}

template <class V>
List<Str*>* dict_keys(const std::vector<std::pair<Str*, V>>& items) {
  auto result = NewList<Str*>();
  int n = items.size();
  for (int i = 0; i < n; ++i) {
    Str* s = items[i].first;  // nullptr for deleted entries
    if (s) {
      result->append(s);
    }
  }
  return result;
}

template <class V>
List<V>* dict_values(const std::vector<std::pair<Str*, V>>& items) {
  auto result = NewList<V>();
  int n = items.size();
  for (int i = 0; i < n; ++i) {
    auto& pair = items[i];
    if (pair.first) {
      result->append(pair.second);
    }
  }
  return result;
}

// Dict currently implemented by VECTOR OF PAIRS.  TODO: Use a real hash table,
// and measure performance.  The hash table has to beat this for small cases!
template <class K, class V>
class Dict : public Obj {
 public:
  Dict() : Obj(Tag::FixedSize, kZeroMask, 0), items_() {
  }

  // Dummy
  Dict(std::initializer_list<K> keys, std::initializer_list<V> values)
      : Obj(Tag::FixedSize, kZeroMask, 0), items_() {
  }

  // d[key] in Python: raises KeyError if not found
  V index_(K key);

  // Get a key.
  // Returns nullptr if not found (Can't use this for non-pointer types?)
  V get(K key);

  // Get a key, but return a default if not found.
  // expr_parse.py uses this with OTHER_BALANCE
  V get(K key, V default_val);

  // d->set(key, val) is like (*d)[key] = val;
  void set(K key, V val);

  void remove(K key);

  List<K>* keys();

  // For AssocArray transformations
  List<V>* values();

  void clear();

  // std::unordered_map<K, V> m_;
  std::vector<std::pair<K, V>> items_;

 private:

  // returns the position in the array
  int find(K key);
};

#include <mycpp/oldstl_dict_impl.h>

template <typename K, typename V>
Dict<K, V>* NewDict() {
  auto self = Alloc<Dict<K, V>>();
  return self;
}

template <typename K, typename V>
Dict<K, V>* NewDict(std::initializer_list<K> keys,
                    std::initializer_list<V> values) {
  // TODO(Jesse): Is this NotImplemented() or InvalidCodePath() ?
  assert(0);  // Uncalled
}

//
// Overloaded free function len()
//

template <typename K, typename V>
inline int len(const Dict<K, V>* d) {
  assert(0);
}

template <typename V>
inline int len(const Dict<Str*, V>* d) {
  int len = 0;
  int n = d->items_.size();
  for (int i = 0; i < n; ++i) {
    Str* s = d->items_[i].first;  // nullptr for deleted entries
    if (s) {
      len++;
    }
  }
  return len;
}

//
// Free functions
//

template <typename K, typename V>
inline bool dict_contains(Dict<K, V>* haystack, K needle) {
  return find_by_key(haystack->items_, needle) != -1;
}

// specialization for Str only
inline void mysort(std::vector<Str*>* v) {
  std::sort(v->begin(), v->end(), _cmp);
}

#endif  // OLDSTL_CONTAINERS_H
