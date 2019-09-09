#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "saga.h"

using namespace saga;

int
test_onnx_main(int argc, char **argv)
{
  if(argc < 4) {
    fprintf(stderr, "Usage .. onnx <input> <inputname> <model> <output>\n");
    exit(1);
  }

  auto input = Tensor::createFromPB(argv[0]);
  if(!input)
    exit(1);

  Network n(1, false);

  auto inputLayer = n.nameLayer(n.addLayer(makeInput(input.get())), argv[1]);

  n.load(argv[2]);

  n.forward(false);

  auto out = n.layers_[n.layers_.size() - 1];
  out->output()->dump("OUTPUT");

  auto ref = Tensor::createFromPB(argv[3]);
  ref->dump("REFERENCE");

  return 0;
}