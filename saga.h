// -*-c++-*-

/*
 * Copyright (c) 2019, Andreas Smas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <variant>
#include <unordered_set>
#include <optional>

#include <assert.h>

namespace saga {


//------------------------------------------------------------------------
//------------------------------------------------------------------------

typedef std::variant<float, int, std::vector<int>, bool> Attribute;

class Attributes : public std::unordered_map<std::string, Attribute> {

public:

  using std::unordered_map<std::string, Attribute>::unordered_map;

  template< typename T > T get(const std::string &n, T def) const {
    auto it = find(n);
    if(it == end())
      return def;
    auto p = std::get_if<T>(&it->second);
    if(p == NULL)
      return def;
    return *p;
  }
};


//------------------------------------------------------------------------
//------------------------------------------------------------------------

class Tensors;

typedef std::vector<int64_t> Dims;

class TensorAccess {

protected:
  TensorAccess() {};
public:
  virtual ~TensorAccess() {};
  virtual Dims strides() = 0;
  virtual void *data() = 0;

  virtual double get(const std::vector<int64_t> &element) const = 0;
  virtual void set(const std::vector<int64_t> &element, double value) = 0;

  TensorAccess(TensorAccess const&) = delete;
  TensorAccess& operator=(TensorAccess const&) = delete;
};


class Tensor {

public:

  struct Stats {
    double min;
    double max;
    double mean;
    double stddev;
  };

  enum class DataType {
    U8,
    HALF,
    FLOAT,
    INT64,
  };

  static size_t DataTypeSize(DataType dt);

  Tensor(DataType data_type, const Dims &size,
         const std::optional<const std::string> &name = std::nullopt);

  virtual ~Tensor() {};

  virtual std::string info() const;

  // Make this const?
  virtual std::unique_ptr<TensorAccess> access() { return nullptr; }

  virtual std::shared_ptr<Tensor> slice(const Dims &offset, const Dims &size) {
    return nullptr;
  }

  void copyFrom(Tensor &t);

  double sse(Tensor &t);

  static std::shared_ptr<Tensor> load(const char *path);

  static std::shared_ptr<Tensor> find(Tensor::DataType data_type,
                                      const Dims &size,
                                      double init_mean,
                                      double init_stddev,
                                      Tensors &named_tensors,
                                      const std::optional<const std::string> &name = std::nullopt);

  // Info / Debug / etc
  void print(const char *prefix, int elements_per_rank = 0);
  void printRGB(const char *prefix);
  Stats stats();
  void printStats(const char *prefix);
  std::string statsString(void);

  std::optional<const std::string> namePostfix(const std::string &postfix);

  const std::optional<const std::string> name_;
  const DataType data_type_;
  const Dims dims_;
  const int64_t elements_;

};


std::shared_ptr<Tensor> makeCPUTensor(Tensor::DataType data_type,
                                      const Dims &size,
                                      const std::optional<const std::string> &name = std::nullopt);

class Tensors : public std::unordered_map<std::string,
                                          std::shared_ptr<Tensor>> {
public:
  using std::unordered_map<std::string, std::shared_ptr<Tensor>>::unordered_map;

  std::shared_ptr<Tensor> get(const std::string &n) const {
    auto it = find(n);
    return it == end() ? nullptr : it->second;
  }

  std::vector<std::shared_ptr<Tensor>> getv(const std::string &n) const {
    std::vector<std::shared_ptr<Tensor>> v;
    for(int i = 0; ; i++) {
      auto it = find(n + std::to_string(i));
      if(it == end())
        break;
      v.push_back(it->second);
    }
    return v;
  }
};

//------------------------------------------------------------------------
//------------------------------------------------------------------------

class Node {
public:

  Node(const std::string &type,
       const std::optional<const std::string> &name = std::nullopt)
    : type_(type)
    , name_(name)
  {}

  const std::string type_;
  const std::optional<const std::string> name_;

  Tensors inputs_;
  Attributes attributes_;
  Tensors outputs_;

  std::shared_ptr<Tensor> inferTensor_y(const std::optional<const std::string> &name = std::nullopt);
  void print() const;

  static std::vector<std::shared_ptr<Node>> make(const std::string &type,
                                                 const Tensors &inputs,
                                                 const Attributes &attributes,
                                                 Tensors &named_tensors,
                                                 const std::optional<const std::string> &name = std::nullopt);
};


//------------------------------------------------------------------------
//------------------------------------------------------------------------

class Graph {
public:
  std::vector<std::shared_ptr<Node>> nodes_;
  std::unordered_set<std::shared_ptr<Tensor>> inputs_;
  std::unordered_set<std::shared_ptr<Tensor>> outputs_;
  Tensors tensors_;

  std::shared_ptr<Tensor> addNode(const std::string &type,
                                  const Tensors &inputs,
                                  const Attributes &attributes,
                                  const std::optional<const std::string> &name = std::nullopt);

  static std::shared_ptr<Graph> load(const char *path);

  void print() const;
};


//------------------------------------------------------------------------
//------------------------------------------------------------------------


enum class ProgramType {
  INFERENCE,
  TRAINING
};


class Program {
public:
  virtual ~Program() {}
  virtual void exec() = 0;
  virtual void print() const = 0;

  std::unordered_set<std::shared_ptr<Tensor>> inputs_;
  std::unordered_set<std::shared_ptr<Tensor>> outputs_;
};

//------------------------------------------------------------------------
//------------------------------------------------------------------------


class Context {
public:
  virtual ~Context() {}
  virtual std::shared_ptr<Program> createProgram(const Graph &graph,
                                                 ProgramType type,
                                                 int batch_size) = 0;
};


std::shared_ptr<Context> createContext();

}
