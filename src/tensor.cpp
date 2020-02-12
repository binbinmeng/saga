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

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

#include <random>
#include <sstream>

#include <inttypes.h>
#include <x86intrin.h>
#include <math.h>


#include "saga.h"
#include "tensor.h"

#include "turbo_colormap.h"

namespace saga {

//------------------------------------------------------------------------

static double
get_float(const void *base, size_t offset)
{
  return ((const float *)base)[offset];
}

static double
get_half(const void *base, size_t offset)
{
  return _cvtsh_ss(((const uint16_t *)base)[offset]);
}

static double
get_u8(const void *base, size_t offset)
{
  return ((const uint8_t *)base)[offset];
}

static double
get_i64(const void *base, size_t offset)
{
  return ((const int64_t *)base)[offset];
}

static double
get_i32(const void *base, size_t offset)
{
  return ((const int32_t *)base)[offset];
}

static void
set_float(void *base, size_t offset, double v)
{
  ((float *)base)[offset] = v;
}

static void
set_half(void *base, size_t offset, double v)
{
  ((uint16_t *)base)[offset] = _cvtss_sh((float)v, 0);
}

static void
set_u8(void *base, size_t offset, double v)
{
  ((uint8_t *)base)[offset] = v;
}

static void
set_i64(void *base, size_t offset, double v)
{
  ((int64_t *)base)[offset] = v;
}

static void
set_i32(void *base, size_t offset, double v)
{
  ((int32_t *)base)[offset] = v;
}

TensorStorage::getfn_t *
datatype_get(Tensor::DataType dt)
{
  switch(dt) {
  case Tensor::DataType::U8:    return &get_u8;
  case Tensor::DataType::HALF:  return &get_half;
  case Tensor::DataType::FLOAT: return &get_float;
  case Tensor::DataType::INT64: return &get_i64;
  case Tensor::DataType::I32:   return &get_i32;
  default: abort();
  }
}

TensorStorage::setfn_t *
datatype_set(Tensor::DataType dt)
{
  switch(dt) {
  case Tensor::DataType::U8:    return &set_u8;
  case Tensor::DataType::HALF:  return &set_half;
  case Tensor::DataType::FLOAT: return &set_float;
  case Tensor::DataType::INT64: return &set_i64;
  case Tensor::DataType::I32:   return &set_i32;
  default: abort();
  }
}


const char *
datatype_str(Tensor::DataType dt)
{
  switch(dt) {
  case Tensor::DataType::U8:    return "u8";
  case Tensor::DataType::HALF:  return "half";
  case Tensor::DataType::FLOAT: return "float";
  case Tensor::DataType::INT64: return "i64";
  case Tensor::DataType::I32:   return "i32";
  default: return "?";
  }
}

size_t
Tensor::DataTypeSize(DataType dt)
{
  switch(dt) {
  case Tensor::DataType::U8:    return 1;
  case Tensor::DataType::HALF:  return 2;
  case Tensor::DataType::FLOAT: return 4;
  case Tensor::DataType::INT64: return 8;
  case Tensor::DataType::I32:   return 4;
  default: abort();
  }
}




TensorStorage::TensorStorage(Tensor::DataType data_type)
  : get_(datatype_get(data_type))
  , set_(datatype_set(data_type))
  , data_type_(data_type)
  , element_size_(Tensor::DataTypeSize(data_type))
  , data_(NULL)
{}


//------------------------------------------------------------------------


int64_t
elements_from_dims(const Dims &dims)
{
  int64_t elements = 1;
  for(auto &d : dims)
    elements *= d;
  return elements;
}



Tensor::Tensor(DataType data_type, const Dims &dims,
               const std::optional<const std::string> &name)
  : name_(name)
  , data_type_(data_type)
  , dims_(dims)
  , elements_(elements_from_dims(dims))
{};



std::string
Tensor::info() const
{
  std::stringstream ss;
  if(name_) {
    ss << "\"" << *name_ << "\"";
  }
  ss << "<" << datatype_str(data_type_) << ">";
  const char *prefix = "[";
  for(const auto &x : dims_) {
    ss << prefix << x;
    prefix = ", ";
  }
  ss << "]";
  return ss.str();
}


static void
print1dTensor(const char *prefix, Tensor &t, TensorAccess &ta)
{
  for(int i = 0; i < t.dims_[0]; i++) {
    printf("%s: [%5d]: %f\n", prefix, (int)i, ta.get({i}));
  }
}


Tensor::Stats
Tensor::stats()
{
  auto ta = access();
  if(ta == nullptr) {
    return Stats({});
  }
  Dims c(dims_.size(), 0);

  double max = -INFINITY;
  double min = INFINITY;
  double sum = 0;

  for(int64_t i = 0; i < elements_; i++) {
    const double v = ta->get(c);

    max = std::max(max, v);
    min = std::min(min, v);
    sum += v;

    for(ssize_t j = c.size() - 1; j >= 0; j--) {
      c[j]++;
      if(c[j] == dims_[j]) {
        c[j] = 0;
      } else {
        break;
      }
    }
  }

  const double mean = sum / elements_;
  double sum2 = 0;
  for(int64_t i = 0; i < elements_; i++) {
    const double v = ta->get(c) - mean;
    sum2 += v * v;
    for(ssize_t j = c.size() - 1; j >= 0; j--) {
      c[j]++;
      if(c[j] == dims_[j]) {
        c[j] = 0;
      } else {
        break;
      }
    }
  }
  return Stats({.min = min, .max = max, .mean = mean,
        .stddev = sqrt(sum2 / elements_)});
}


std::string
Tensor::statsString(void)
{
  auto s = stats();
  char buf[512];
  snprintf(buf, sizeof(buf), "{min:%f mean:%f max:%f stddev:%f}",
           s.min, s.mean, s.max, s.stddev);
  return std::string(buf);
}


void
Tensor::printStats(const char *prefix)
{
  auto s = stats();
  printf("%s: min:%f max:%f mean:%f stddev:%f\n", prefix,
         s.min, s.max, s.mean, s.stddev);
}


void
Tensor::print(const char *prefix, int elements_per_rank)
{
  printf("%s: %s\n", prefix, info().c_str());

  auto ta = access();
  if(ta == nullptr) {
    printf("%s: Abstract (no data)\n", prefix);
    return;
  }

  if(dims_.size() == 1) {
    // We format 1d tensor vertically instead of a long horizontal line
    print1dTensor(prefix, *this, *ta);
    return;
  }

  const size_t rank = dims_.size();
  Dims c(rank, 0);

  const char *lf = "";

  while(1) {
    if(c[rank - 1] == 0) {
      printf("%s%s: [", lf, prefix);
      for(size_t j = 0; j < c.size(); j++) {
        printf("%s%3d", j ? "," : "", c[j]);
      }
      printf("]");
      lf = "\n";
    }
    printf(" % 1.6f", ta->get(c));

    for(ssize_t j = rank - 1; j >= 0; j--) {
      c[j]++;
      if(c[j] == dims_[j] || c[j] == elements_per_rank) {
        if(j == 0) {
          printf("%s", lf);
          return;
        }
        c[j] = 0;
      } else {
        break;
      }
    }
  }
}


std::shared_ptr<Tensor>
Tensor::toRGB(std::optional<std::pair<float, float>> range)
{
  if(dims_.size() != 4)
    return nullptr;

  const int in = dims_[0];
  const int ic = dims_[1];
  const int ih = dims_[2];
  const int iw = dims_[3];

  float min, max;

  if(range) {
    min = (*range).first;
    max = (*range).second;
  } else {
    auto s = stats();
    min = s.min;
    max = s.max;
  }

  const float offset = -min;
  const float scale = 255.0f / (max - min);

  auto src = access();
  if(src == nullptr)
    return nullptr;

  Dims odims = dims_;

  if(ic > 3) {
    odims.push_back(3);
  } else {
    odims = Dims{in,1,ih,iw,3};
  }

  auto ot = makeCPUTensor(Tensor::DataType::U8, odims);
  auto dst = ot->access();

  uint8_t *p = (uint8_t *)dst->data();

  int oc = odims[1];
  for(int n = 0; n < in; n++) {
    for(int c = 0; c < oc; c++) {
      for(int y = 0; y < ih; y++) {
        for(int x = 0; x < iw; x++) {

          float r, g, b;
          int i;
          switch(ic) {
          case 3:
            r = (src->get({n,0,y,x}) + offset) * scale;
            g = (src->get({n,1,y,x}) + offset) * scale;
            b = (src->get({n,2,y,x}) + offset) * scale;
            break;
          case 2:
            r = (src->get({n,0,y,x}) + offset) * scale;
            g = (src->get({n,1,y,x}) + offset) * scale;
            b = 0;
            break;
          case 1:
            r = (src->get({n,0,y,x}) + offset) * scale;
            g = r;
            b = r;
            break;
          default:
            r = (src->get({n,c,y,x}) + offset) * scale;
            i = std::clamp(r, 0.0f, 255.0f);
            *p++ = turbo_srgb_bytes[i][0];
            *p++ = turbo_srgb_bytes[i][1];
            *p++ = turbo_srgb_bytes[i][2];
            continue;
          }

          *p++ = std::clamp(r, 0.0f, 255.0f);
          *p++ = std::clamp(g, 0.0f, 255.0f);
          *p++ = std::clamp(b, 0.0f, 255.0f);
        }
      }
    }
  }
  return ot;
}


void
Tensor::printRGB(const char *prefix)
{
  printf("%s: %s\n", prefix, info().c_str());

  auto rgb = toRGB();
  if(rgb == NULL) {
    printf("%s: Too few dimensions or abstract\n", prefix);
    return;
  }

  const int n = rgb->dims_[0];
  const int c = rgb->dims_[1];
  const int h = rgb->dims_[2];
  const int w = rgb->dims_[3];

  auto ta = rgb->access();
  const auto strides = ta->strides();
  const uint8_t *pixels = (const uint8_t *)ta->data();

  for(int a = 0; a < n; a++) {
    for(int b = 0; b < c; b++) {
      printf("%s: [%d,%d]", prefix, a, b);
      for(int x = 0; x < w; x++) {
        printf("=");
      }
      printf("\n");

      const uint8_t *img = pixels + a * strides[0] + b * strides[1];

      for(int y = 0; y < h; y += 2) {
        printf("%s: [%d,%d]", prefix, a, b);

        const uint8_t *r1 = img + strides[2] * y;
        const uint8_t *r2 = y < h - 1 ? r1 + strides[2] : NULL;

        for(int x = 0; x < w; x++) {
          if(r2) {
            const uint8_t r = *r2++;
            const uint8_t g = *r2++;
            const uint8_t b = *r2++;
            printf("\033[48;2;%d;%d;%dm", r, g, b);
          }
          const uint8_t r = *r1++;
          const uint8_t g = *r1++;
          const uint8_t b = *r1++;
          printf("\033[38;2;%d;%d;%dm▀", r, g, b);
        }
        printf("\033[0m\n");
      }
    }
  }
}


void
Tensor::copyFrom(Tensor &t)
{
  auto src = t.access();
  if(src == nullptr)
    return;

  auto dst = access();

  assert(t.elements_ == elements_);

  Dims c_s(t.dims_.size(), 0);
  Dims c_d(dims_.size(), 0);

  // This is very slow
  for(int64_t i = 0; i < elements_; i++) {
    dst->set(c_d, src->get(c_s));

    for(ssize_t j = c_d.size() - 1; j >= 0; j--) {
      c_d[j]++;
      if(c_d[j] == dims_[j]) {
        c_d[j] = 0;
      } else {
        break;
      }
    }

    for(ssize_t j = c_s.size() - 1; j >= 0; j--) {
      c_s[j]++;
      if(c_s[j] == t.dims_[j]) {
        c_s[j] = 0;
      } else {
        break;
      }
    }
  }
}


double
Tensor::sse(Tensor &t)
{
  auto a = t.access();
  auto b = access();
  if(a == nullptr && b == nullptr)
    return 0.0;

  if(a == nullptr || b == nullptr)
    return INFINITY;

  assert(t.elements_ == elements_);

  Dims c_a(t.dims_.size(), 0);
  Dims c_b(dims_.size(), 0);

  double r = 0;
  for(int64_t i = 0; i < elements_; i++) {

    double v = a->get(c_a) - b->get(c_b);
    r += v * v;

    for(ssize_t j = c_a.size() - 1; j >= 0; j--) {
      c_a[j]++;
      if(c_a[j] == dims_[j]) {
        c_a[j] = 0;
      } else {
        break;
      }
    }

    for(ssize_t j = c_b.size() - 1; j >= 0; j--) {
      c_b[j]++;
      if(c_b[j] == t.dims_[j]) {
        c_b[j] = 0;
      } else {
        break;
      }
    }
  }
  return r;
}


std::optional<const std::string>
Tensor::namePostfix(const std::string &postfix) const
{
  return name_ ? std::make_optional(*name_ + "." + postfix) : std::nullopt;
}




class GenTensorAccess : public TensorAccess {

public:
  GenTensorAccess(size_t rank, double mean, double stddev)
    : rank_(rank)
    , distribution_(mean, stddev)
  {}

  Dims strides() { return Dims(rank_, 0); }

  void *data() {
    return NULL;
  }

  virtual double get(const Dims &element) {
    return distribution_(generator_);
  };

  virtual void set(const Dims &element, double value) {

  }

  virtual void copyBytesFrom(const Dims &element,
                             const void *data, size_t size)
  {

  }

  const size_t rank_;

  mutable std::normal_distribution<double> distribution_;
  mutable std::default_random_engine generator_;
};




class GenTensor : public Tensor {
public:
  GenTensor(DataType data_type, const Dims &size,
            const std::optional<std::string> &name,
            double mean, double stddev)
    : Tensor(data_type, size, name)
    , mean_(mean)
    , stddev_(stddev)
  {}

  std::unique_ptr<TensorAccess> access() {
    return std::make_unique<GenTensorAccess>(dims_.size(), mean_, stddev_);
  }

  std::shared_ptr<Tensor> slice(const Dims &offset, const Dims &size) {
    return std::make_shared<GenTensor>(data_type_, size, name_,
                                       mean_, stddev_);
  }

  std::string info() const {
    std::stringstream ss;
    ss << Tensor::info();
    ss << "(mean:" << mean_ << ", stddev:" << stddev_ << ")";
    return ss.str();
  }

  const double mean_;
  const double stddev_;
};





std::shared_ptr<Tensor>
Tensor::find(Tensor::DataType data_type,
             const Dims &size,
             double init_mean,
             double init_stddev,
             Tensors &named_tensors,
             const std::optional<const std::string> &name)
{
  if(name) {
    auto it = named_tensors.find(*name);
    if(it != named_tensors.end()) {
      auto t = it->second;
      assert(t->data_type_ == data_type);
      assert(t->dims_ == size);
      return t;
    }
  }

  auto t = std::make_shared<GenTensor>(data_type, size, name,
                                       init_mean, init_stddev);

  if(name)
    named_tensors[*name] = t;

  return t;
}

std::shared_ptr<Tensor>
Tensor::make(Tensor::DataType data_type,
             const Dims &size,
             double init_mean,
             double init_stddev)
{
  return std::make_shared<GenTensor>(data_type, size, std::nullopt,
                                     init_mean, init_stddev);
}






//------------------------------------------------------------------------

class HostTensorStorage : public TensorStorage {

  const size_t buffer_size_;
  void *mmaped_;

public:
  HostTensorStorage(Tensor::DataType data_type, const Dims &size,
                    const Dims &strides)
    : TensorStorage(data_type)
    , buffer_size_(size[0] * strides[0] * Tensor::DataTypeSize(data_type))
    , mmaped_(NULL)
  {
    data_ = calloc(1, buffer_size_);
  }

  HostTensorStorage(Tensor::DataType data_type, const Dims &size,
                    const Dims &strides,
                    void *mmaped_memory, size_t buffer_size,
                    void *data)
    : TensorStorage(data_type)
    , buffer_size_(buffer_size)
    , mmaped_(mmaped_memory)
  {
    data_ = data;
  }

  ~HostTensorStorage()
  {
    if(mmaped_) {
      munmap(mmaped_, buffer_size_);
    } else {
      free(data_);
    }
  }
};



//------------------------------------------------------------------------

class CPUTensorAccess : public TensorAccess {

public:
  CPUTensorAccess(const Dims &strides,
                  std::shared_ptr<TensorStorage> storage,
                  int64_t offset)
    : strides_(strides)
    , storage_(storage)
    , offset_(offset)
  {}

  ~CPUTensorAccess() {}

  Dims strides() { return strides_; }

  void *data() {
    assert(offset_ == 0);
    return storage_->data_;
  }

  size_t offsetForElement(const Dims &element) const {
    size_t offset = offset_;
    for(size_t i = 0; i < element.size() && i < strides_.size(); i++) {
      offset += element[i] * strides_[i];
    }
    return offset;
  }

  virtual double get(const Dims &element) {
    return storage_->get(offsetForElement(element));
  };

  virtual void set(const Dims &element, double value) {
    storage_->set(offsetForElement(element), value);
  }

  virtual void copyBytesFrom(const Dims &element,
                             const void *data, size_t size)
  {
    const size_t o = offsetForElement(element) * storage_->element_size_;
    char *dst = (char *)storage_->data_;
    memcpy(dst + o, data, size);
  }

  const Dims strides_;
  const std::shared_ptr<TensorStorage> storage_;
  int64_t offset_;
};


//------------------------------------------------------------------------


static Dims
computeCPUStrides(const Dims &dims)
{
  Dims strides;
  int stride = 1;
  for(int i = dims.size() - 1; i >= 0; i--) {
    strides.insert(strides.begin(), stride);
    stride *= dims[i];
  }
  return strides;
}





class CPUTensor : public Tensor {
public:
  CPUTensor(DataType data_type, const Dims &size,
            const std::optional<std::string> &name)
    : Tensor(data_type, size, name)
    , strides_(computeCPUStrides(size))
    , storage_(std::make_shared<HostTensorStorage>(data_type, size, strides_))
    , offset_(0)
  {}

  CPUTensor(DataType data_type, const Dims &size,
            const Dims &strides,
            const std::optional<std::string> &name)
    : Tensor(data_type, size, name)
    , strides_(strides)
    , storage_(std::make_shared<HostTensorStorage>(data_type, size, strides_))
    , offset_(0)
  {}

  CPUTensor(const Dims &size, const Dims &strides,
            std::shared_ptr<TensorStorage> storage, int64_t offset,
            const std::optional<std::string> &name)
    : Tensor(storage->data_type_, size, name)
    , strides_(strides)
    , storage_(storage)
    , offset_(offset)
  {}

  std::unique_ptr<TensorAccess> access() {
    return std::make_unique<CPUTensorAccess>(strides_, storage_, offset_);
  }

  std::shared_ptr<Tensor> slice(const Dims &offset, const Dims &size);

  virtual std::string info() const;

  const Dims strides_;
  const std::shared_ptr<TensorStorage> storage_;
  const int64_t offset_;
};


std::shared_ptr<Tensor>
CPUTensor::slice(const Dims &offset, const Dims &size)
{
  int64_t o = offset_;

  for(size_t i = 0; i < strides_.size() && i < offset.size(); i++) {
    o += offset[i] * strides_[i];
  }

  return std::make_shared<CPUTensor>(size, strides_,
                                     storage_, o,
                                     namePostfix("slice"));
}


std::string
CPUTensor::info() const
{
  std::stringstream ss;

  ss << Tensor::info();
  const char *prefix = "{";
  for(const auto &x : strides_) {
    ss << prefix << x;
    prefix = ", ";
  }
  ss << "}";
  return ss.str();
}



std::shared_ptr<Tensor>
makeCPUTensor(Tensor::DataType data_type, const Dims &size,
              const std::optional<const std::string> &name)
{
  return std::make_shared<CPUTensor>(data_type, size, name);
}


//------------------------------------------------------------------------
// Raw tensor disk IO.

typedef enum {
  TENSOR_DISK_FLOAT = 0,
  TENSOR_DISK_HALF  = 1,
} TensorDiskType;

struct TensorDiskHeader {
  uint8_t magic[8];
  TensorDiskType type;
  unsigned int rank;
} __attribute__((packed));


std::shared_ptr<Tensor>
Tensor::load(const char *path, const std::optional<const std::string> &name)
{
  int fd = open(path, O_RDONLY);
  if(fd == -1) {
    fprintf(stderr, "Unable to open %s -- %s\n", path, strerror(errno));
    return nullptr;
  }

  struct stat st;
  if(fstat(fd, &st)) {
    fprintf(stderr, "Unable to stat %s -- %s\n", path, strerror(errno));
    close(fd);
    return nullptr;
  }

  if((size_t)st.st_size < sizeof(TensorDiskHeader)) {
    fprintf(stderr, "Unable to load %s -- Not a saga tensor file\n", path);
    close(fd);
    return nullptr;
  }

  void *mem = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if(mem == MAP_FAILED) {
    fprintf(stderr, "Unable to load %s -- Out of memory\n", path);
    close(fd);
    return nullptr;
  }
  close(fd);

  const TensorDiskHeader *tdh = (const TensorDiskHeader *)mem;
  if(memcmp(tdh->magic, "sagaT001", 8)) {
    fprintf(stderr, "Unable to load %s -- Not a saga tensor file\n", path);
    munmap(mem, st.st_size);
    return nullptr;
  }

  Tensor::DataType data_type;

  switch(tdh->type) {
  case TENSOR_DISK_FLOAT:
    data_type = Tensor::DataType::FLOAT;
    break;
  case TENSOR_DISK_HALF:
    data_type = Tensor::DataType::HALF;
    break;
  default:
    fprintf(stderr, "Unable to load %s -- Unsupported data type:%d\n",
            path, tdh->type);
    munmap(mem, st.st_size);
    return nullptr;
  }

  if(tdh->rank > 8) {
    fprintf(stderr, "Unable to load %s -- Rank %d too high\n",
            path, tdh->rank);
    munmap(mem, st.st_size);
    return nullptr;
  }

  if((size_t)st.st_size < sizeof(TensorDiskHeader) + tdh->rank * sizeof(int)) {
    fprintf(stderr, "Unable to load %s -- File too short\n",
            path);
    munmap(mem, st.st_size);
    return nullptr;
  }

  Dims dims;
  const unsigned int *d =
    (unsigned int *)((char *)mem + sizeof(TensorDiskHeader));

  for(unsigned int i = 0; i < tdh->rank; i++) {
    dims.push_back(*d++);
  }

  auto strides = computeCPUStrides(dims);

  // Ownership of mmap:ed region 'mem' is transfered to HostTensorStorage
  auto storage = std::make_shared<HostTensorStorage>(data_type, dims, strides,
                                                     mem, st.st_size,
                                                     (void *)d);

  return std::make_shared<CPUTensor>(dims, strides, storage, 0, name);
}


bool
Tensor::save(const char *path)
{
  TensorDiskHeader tdh;
  memcpy(&tdh.magic, "sagaT001", 8);

  switch(data_type_) {
  case Tensor::DataType::FLOAT:
    tdh.type = TENSOR_DISK_FLOAT;
    break;
  case Tensor::DataType::HALF:
    tdh.type = TENSOR_DISK_HALF;
    break;
  default:
    fprintf(stderr, "Unable to load %s -- Unsupported data type:%d\n",
            path, (int)data_type_);
    return false;
  }

  int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if(fd == -1) {
    fprintf(stderr, "Unable to save %s -- %s\n", path, strerror(errno));
    return false;
  }

  tdh.rank = dims_.size();

  if(write(fd, &tdh, sizeof(tdh)) != sizeof(tdh)) {
    fprintf(stderr, "Unable to save %s -- %s\n", path, strerror(errno));
    close(fd);
    return false;
  }

  uint32_t ondiskdims[tdh.rank];
  for(unsigned int i = 0; i < tdh.rank; i++) {
    ondiskdims[i] = dims_[i];
  }

  if(write(fd, &ondiskdims[0], sizeof(int) * tdh.rank) !=
     (int)(sizeof(int) * tdh.rank)) {
    fprintf(stderr, "Unable to save %s -- %s\n", path, strerror(errno));
    close(fd);
    return false;
  }

  CPUTensor copy(data_type_, dims_, std::nullopt);
  copy.copyFrom(*this);

  auto ta = copy.access();

  size_t size =
    copy.strides_[0] * copy.dims_[0] * Tensor::DataTypeSize(data_type_);

  if(write(fd, ta->data(), size) != (int)size) {
    fprintf(stderr, "Unable to save %s -- %s\n", path, strerror(errno));
    close(fd);
    return false;
  }
  close(fd);
  return true;
}


}



