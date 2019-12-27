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

#include "saga.h"

namespace saga {

std::shared_ptr<Tensor>
makeConvOutputTensor(const std::string &name, const Node &n)
{
  // Should make this more generic for n-dimensions

  const int stride = n.attributes_.get("stride", 1);
  const int pad = n.attributes_.get("pad", 1);
  const int dilation = n.attributes_.get("dilation", 1);
  auto w = n.inputs_.get("w");
  if(w == nullptr)
    return nullptr;
  auto x = n.inputs_.get("x");
  if(x == nullptr)
    return nullptr;

  const int features = w->dims_[0];

  const int filterdim_h = w->dims_[2];
  const int filterdim_w = w->dims_[3];

  const int inputdim_h = x->dims_[2];
  const int inputdim_w = x->dims_[3];

  const int outputdim_w =
    1 + (inputdim_w + 2 * pad - (((filterdim_w - 1) * dilation) + 1))/stride;

  const int outputdim_h =
    1 + (inputdim_h + 2 * pad - (((filterdim_h - 1) * dilation) + 1))/stride;

  return std::make_shared<Tensor>(name, x->data_type_,
                                  Dims({1, features,
                                        outputdim_h, outputdim_w}));
}


std::shared_ptr<Tensor>
makePoolingOutputTensor(const std::string &name, const Node &n)
{
  // Should make this more generic for n-dimensions

  const int size = n.attributes_.get("size", 1);
  const int pad = n.attributes_.get("pad", 1);
  const int stride = n.attributes_.get("stride", 1);
  auto x = n.inputs_.get("x");
  if(x == nullptr)
    return nullptr;

  const int channels   = x->dims_[1];
  const int inputdim_h = x->dims_[2];
  const int inputdim_w = x->dims_[3];

  const int outputdim_h =
    1 + (inputdim_h + 2 * pad - size) / stride;
  const int outputdim_w =
    1 + (inputdim_w + 2 * pad - size) / stride;

  return std::make_shared<Tensor>(name, x->data_type_,
                                  Dims({1, channels,
                                        outputdim_h, outputdim_w}));
}

std::shared_ptr<Tensor>
makeReshapeOutputTensor(const std::string &name, const Node &n)
{
  auto x = n.inputs_.get("x");
  if(x == nullptr)
    return nullptr;
  auto shape = n.inputs_.get("shape");
  if(shape == nullptr)
    return nullptr;

  if(shape->dims_.size() != 1) {
    fprintf(stderr, "Shape tensor is not 1d\n");
    return nullptr;
  }

  auto ta = shape->access();

  Dims dims;
  for(int64_t i = 0; i < shape->dims_[0]; i++) {
    dims.push_back(ta->get({i}));
  }

  return std::make_shared<Tensor>(name, x->data_type_, dims);
}

std::shared_ptr<Tensor>
makeFCOutputTensor(const std::string &name, const Node &n)
{
  auto w = n.inputs_.get("w");
  if(w == nullptr)
    return nullptr;

  return std::make_shared<Tensor>(name, w->data_type_,
                                  Dims({1, w->dims_[0]}));
}


std::shared_ptr<Tensor>
makeOutputTensorFromBlueprint(const std::string &name, const char *blueprint,
                              const Node &n)
{
  auto o = n.inputs_.get(blueprint);
  if(o == nullptr)
    return nullptr;
  return std::make_shared<Tensor>(name, o->data_type_, o->dims_);
}

std::shared_ptr<Tensor>
Node::makeOutputTensor(const std::string &name)
{
  std::shared_ptr<Tensor> y;

  switch(type_) {
  case Type::CONV:
    y = makeConvOutputTensor(name, *this);
    break;
  case Type::FC:
    y = makeFCOutputTensor(name, *this);
    break;
  case Type::MAXPOOL:
  case Type::AVGPOOL:
    y = makePoolingOutputTensor(name, *this);
    break;
  case Type::RESHAPE:
    y = makeReshapeOutputTensor(name, *this);
    break;
  case Type::BATCHNORM:
  case Type::SOFTMAX:
  case Type::RELU:
  case Type::DROPOUT:
    y = makeOutputTensorFromBlueprint(name, "x", *this);
    break;
  case Type::SUM:
    y = makeOutputTensorFromBlueprint(name, "x0", *this);
    break;

  default:
    break;
  }
  if(y == nullptr) {
    fprintf(stderr, "Can't compute output tensor for type %d\n", (int)type_);
    abort();
  }
  outputs_["y"] = y;
  return y;
}

}
