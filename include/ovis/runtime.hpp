#pragma once

#include "ovis/runtime.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <utility>


inline intptr_t max_align() {
  return 1;
}

template <typename... T>
intptr_t max_align(const TypeInfo* info, T&&... infos) {
  return std::max(info->align, max_align(std::forward<T>(infos)...));
}

template <typename T = void>
class Variable {
 public:
  Variable(const TypeInfo* type) {
    m_value = (T*)aligned_alloc(type->align, type->size);
    m_type = type;
    m_initialized = false;
  }

  ~Variable() {
    if (m_initialized) {
      m_type->destroy(m_type, m_value);
    }
    free(m_value);
  }

  Variable(const Variable&) = delete;
  Variable(Variable&&) = delete;

  Variable operator=(const Variable&) = delete;
  Variable operator=(Variable&&) = delete;

  void initialize() {
    assert(!m_initialized);
    m_type->initialize(m_type, m_value);
    m_initialized = true;
  }

  void destroy() {
    assert(m_initialized);
    m_type->destroy(m_type, m_value);
    m_initialized = false;
  }

  T* get_uninitialized() {
    assert(!m_initialized);
    return m_value;
  }

  T* get() {
    assert(m_initialized);
    return m_value;
  }

  void set(const T* value) {
    if (m_initialized) {
      m_type->destroy(m_type, m_value);
    }
    m_type->clone(m_type, value, m_value);
  }

 private:
  const TypeInfo* m_type;
  T* m_value;
  bool m_initialized;
};
