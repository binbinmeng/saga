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

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include "saga.h"

namespace saga {

void
Graph::print() const
{
  for(const auto &n : nodes_) {
    n->print();
  }
}

std::shared_ptr<Node>
Graph::addNode(const std::string &type,
               const Tensors &inputs,
               const Attributes &attributes,
               const std::optional<const std::string> &name)
{
  auto nodes = Node::make(type, inputs, attributes, tensors_, name);
  nodes_.insert(nodes_.end(), nodes.begin(), nodes.end());
  if(nodes_.size() == 0)
    return nullptr;
  return nodes_[nodes_.size() - 1];
}

std::shared_ptr<Node>
Graph::addNode(const std::string &type,
               Loader loader,
               const Attributes &attributes)
{
  auto nodes = Node::make(type, loader, attributes);
  nodes_.insert(nodes_.end(), nodes.begin(), nodes.end());
  if(nodes_.size() == 0)
    return nullptr;
  return nodes_[nodes_.size() - 1];
}



std::unordered_set<std::shared_ptr<Tensor>>
Graph::inputTensors() const
{
  std::unordered_set<std::shared_ptr<Tensor>> inputs;

  for(const auto &n : nodes_) {
    for(auto &t : n->inputs_) {
      inputs.insert(t.second);
    }
  }
  for(const auto &n : nodes_) {
    for(auto &t : n->outputs_) {
      inputs.erase(t.second);
    }
  }
  return inputs;
}



std::unordered_set<std::shared_ptr<Tensor>>
Graph::outputTensors() const
{
  std::unordered_set<std::shared_ptr<Tensor>> outputs;

  for(const auto &n : nodes_) {
    for(auto &t : n->outputs_) {
      outputs.insert(t.second);
    }
  }
  for(const auto &n : nodes_) {
    for(auto &t : n->inputs_) {
      outputs.erase(t.second);
    }
  }
  return outputs;
}


std::pair<TensorMapping, TensorMapping>
Graph::tensorMappings() const
{
  std::unordered_map<std::shared_ptr<Tensor>,
                     std::vector<std::pair<std::string,
                                           std::shared_ptr<Node>>>> input_usage;
  std::unordered_map<std::shared_ptr<Tensor>,
                     std::vector<std::pair<std::string,
                                           std::shared_ptr<Node>>>> output_usage;

  for(const auto &n : nodes_) {
    for(const auto &t : n->inputs_) {
      input_usage[t.second].push_back(std::make_pair(t.first, n));
    }
    for(const auto &t : n->outputs_) {
      output_usage[t.second].push_back(std::make_pair(t.first, n));
    }
  }
  return {input_usage, output_usage};
}


void
Graph::loadTensors(const char *path)
{
  struct dirent **namelist;
  int n = scandir(path, &namelist, NULL, NULL);
  if(n == -1) {
    fprintf(stderr, "Unable to load tensors from %s -- %s\n",
            path, strerror(errno));
    return;
  }

  while(n--) {
    const char *fname = namelist[n]->d_name;
    if(fname[0] != '.') {
      char filepath[PATH_MAX];
      snprintf(filepath, sizeof(filepath), "%s/%s", path, fname);
      auto t = Tensor::load(filepath, fname);
      if(t) {
        tensors_[fname] = t;
        printf("Loaded %s: %s\n", fname, t->info().c_str());
      }
    }
    free(namelist[n]);
  }
  free(namelist);
}


bool
Graph::saveTensors(const char *path, Program *p)
{
  char filepath[PATH_MAX];
  mkdir(path, 0777);
  for(const auto &it : tensors_) {

    auto t = it.second;
    if(p != NULL)
      t = p->resolveTensor(t);

    printf("Saving tensor %s : %s\n", it.first.c_str(),
           t->info().c_str());
    snprintf(filepath, sizeof(filepath), "%s/%s", path, it.first.c_str());
    if(!t->save(filepath))
      return false;
  }
  return true;
}


}
