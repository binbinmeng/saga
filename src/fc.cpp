#include <memory>
#include <algorithm>

#include "common.h"

using namespace std;

namespace saga {


class FullyConnected : public Layer {

public:
  FullyConnected(int num_outputs,
                 const Layer &prev,
                 Network &net,
                 const char *weights,
                 const char *bias)
    : input_(prev.output())
    , num_inputs_(input_->c * input_->h * input_->w)
    , num_outputs_(num_outputs)
    , output_(make_unique<Tensor>(Size(input_->n, num_outputs, 1, 1),
                                  input_->dataType()))
  {
    prev.output()->allocate();

    weights_ = net.findTensor(weights, Size(num_inputs_, num_outputs, 1, 1),
                              input_->dataType(),
                              0.0, sqrt(2.0 / num_inputs_));

    bias_ = net.findTensor(bias, Size(1, num_outputs, 1, 1),
                           input_->dataType(),
                           0.0, 0.0f);
  }

  Tensor *output() const override {
    return output_.get();
  }

  string name() const override {
    stringstream ss;
    ss << "FC " << input_->name() << " => " << output_->name();
    return ss.str();
  }

  // Replace this with cuTensor?

  void forward(const Network &n) override {

    float alpha = 1.0f, beta = 0.0f;

    chkCuda(cublasSgemm(n.cublas_, CUBLAS_OP_T, CUBLAS_OP_N,
                        num_outputs_, input_->n, num_inputs_,
                        &alpha,
                        (const float *)weights_->deviceMem(), num_inputs_,
                        (const float *)input_->deviceMem(), num_inputs_,
                        &beta,
                        (float *)output_->deviceMem(), num_outputs_));

    chkCUDNN(cudnnAddTensor(n.cudnn_,
                            &alpha, bias_->desc(), bias_->deviceMem(),
                            &alpha, output_->desc(), output_->deviceMem()));
  }


protected:

  Tensor *input_;

  const int num_inputs_;
  const int num_outputs_;

  shared_ptr<Tensor> weights_;
  shared_ptr<Tensor> bias_;

  unique_ptr<Tensor> output_;
};


class FullyConnectedBackProp : public FullyConnected {

public:
  FullyConnectedBackProp(int num_outputs,
                         const Layer &prev,
                         Network &n,
                         const char *weights,
                         const char *bias)
    : FullyConnected(num_outputs, prev, n, weights, bias)
    , input_grad_(prev.gradient())
    , weights_grad_(make_unique<Tensor>(*weights_))
    , bias_grad_(make_unique<Tensor>(*bias_))
    , batch_of_one_(Size(input_->n, 1, 1, 1), input_->dataType(), 1.0f)
    , output_grad_(make_unique<Tensor>(*output_))
    , weights_optimizer_(n.makeOptimizer(*weights_.get()))
    , bias_optimizer_(n.makeOptimizer(*bias_.get()))
  {
    prev.gradient()->allocate();
    weights_grad_->allocate();
    bias_grad_->allocate();
  }


  void backprop(const Network &n) override {

    float alpha = 1.0f, beta = 0.0f;

    chkCuda(cublasSgemm(n.cublas_, CUBLAS_OP_N, CUBLAS_OP_T,
                        num_inputs_, num_outputs_, input_->n,
                        &alpha,
                        (const float *)input_->deviceMem(), num_inputs_,
                        (const float *)output_grad_->deviceMem(), num_outputs_,
                        &beta,
                        (float *)weights_grad_->deviceMem(), num_inputs_));

    chkCuda(cublasSgemv(n.cublas_, CUBLAS_OP_N, num_outputs_, input_->n,
                        &alpha,
                        (const float *)output_grad_->deviceMem(), num_outputs_,
                        (const float *)batch_of_one_.deviceMem(), 1,
                        &beta,
                        (float *)bias_grad_->deviceMem(), 1));

    if(input_grad_ != NULL) {
      chkCuda(cublasSgemm(n.cublas_, CUBLAS_OP_N, CUBLAS_OP_N,
                          num_inputs_, input_->n, num_outputs_,
                          &alpha,
                          (const float *)weights_->deviceMem(), num_inputs_,
                          (const float *)output_grad_->deviceMem(), num_outputs_,
                          &beta,
                          (float *)input_grad_->deviceMem(), num_inputs_));
    }

    weights_optimizer_->optimize(*weights_.get(), *weights_grad_, n);
    bias_optimizer_->optimize(*bias_.get(), *bias_grad_, n);
  }

  Tensor *gradient() const {
    return output_grad_.get();
  }

protected:
  const Tensor *input_grad_;
  unique_ptr<Tensor> weights_grad_;
  unique_ptr<Tensor> bias_grad_;
  Tensor batch_of_one_;

  unique_ptr<Tensor> output_grad_;

  unique_ptr<Optimizer> weights_optimizer_;
  unique_ptr<Optimizer> bias_optimizer_;

};


std::shared_ptr<Layer> makeFullyConnected(int num_outputs,
                                          const Layer &prev,
                                          Network &n,
                                          const char *weights,
                                          const char *bias)
{
  if(0) {
    // Fully connected layers can be done via convolution layers,
    // but it's significantly slower
    int w = prev.output()->w;
    int h = prev.output()->h;
    assert(w == h);
    return makeConvolution(num_outputs, w, 1, 0, prev, n, true, weights, bias);
  }


  if(n.backprop_)
    return std::make_shared<FullyConnectedBackProp>(num_outputs, prev, n,
                                                    weights, bias);
  else
    return std::make_shared<FullyConnected>(num_outputs, prev, n, weights,
                                            bias);
}


}
