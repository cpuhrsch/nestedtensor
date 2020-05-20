#include <nestedtensor/csrc/functions.h>
#include <nestedtensor/csrc/nested_tensor_impl.h>
#include <nestedtensor/csrc/utils/nested_node_functions.h>
#include <torch/extension.h>
#include <torch/library.h>

using namespace torch::nn;
namespace F = torch::nn::functional;

namespace at {

Tensor NestedTensor_batch_norm(
    const Tensor& input, const Tensor& weight /* optional */, const Tensor& bias /* optional */,
    const Tensor& running_mean /* optional */, const Tensor& running_var /* optional */,
    bool training, double momentum, double eps, bool cudnn_enabled) {
  auto input_impl = get_nested_tensor_impl(input);
  auto input_data = input_impl->_data;
  auto structure = input_data.get_structure();
  auto res = map(
      [&](at::Tensor t) {
        return at::batch_norm(
                   t.unsqueeze(0),
                   weight,
                   bias,
                   running_mean,
                   running_var,
                   training,
                   momentum,
                   eps,
                   cudnn_enabled)
            .squeeze(0);
      },
      structure);
  return wrap_nested_tensor(std::move(res));
}

Tensor NestedTensor_conv2d(
    const Tensor& input,
    const Tensor& weight,
    const Tensor& bias,
    IntArrayRef stride,
    IntArrayRef padding,
    IntArrayRef dilation,
    int64_t groups) {
  auto input_impl = get_nested_tensor_impl(input);
  auto input_data = input_impl->_data;
  auto structure = input_data.get_structure();

  auto res = map(
      [&weight, &bias, &stride, &padding, &dilation, groups](at::Tensor t) {
        return at::convolution(
                   t.unsqueeze(0),
                   weight,
                   bias,
                   stride,
                   padding,
                   dilation,
                   false,
                   {{0, 0}},
                   groups)
            .squeeze(0);
      },
      structure);

  return at::detail::make_tensor<NestedTensorImpl>(
      torch::nested_tensor::NestedTensor(std::move(res)));
}

Tensor NestedTensor_max_pool2d(
    const Tensor& self,
    IntArrayRef kernel_size,
    IntArrayRef stride,
    IntArrayRef padding,
    IntArrayRef dilation,
    bool ceil_mode) {
  auto self_impl = get_nested_tensor_impl(self);
  auto self_data = self_impl->_data;
  auto structure = self_data.get_structure();
  auto res = map(
      [&](at::Tensor t) {
        return at::max_pool2d(
                   t.unsqueeze(0),
                   kernel_size,
                   stride,
                   padding,
                   dilation,
                   ceil_mode)
            .squeeze(0);
      },
      structure);

  return at::detail::make_tensor<NestedTensorImpl>(
      torch::nested_tensor::NestedTensor(std::move(res)));
}

Tensor NestedTensor_relu(const Tensor& self) {
  auto self_impl = get_nested_tensor_impl(self);
  auto self_data = self_impl->_data;
  if (self_data.is_contiguous()) {
    auto res = torch::nested_tensor::NestedTensor(
        at::relu(*self_data.get_buffer()), self_data.nested_size());
    return at::detail::make_tensor<NestedTensorImpl>(std::move(res));
  }
  auto structure = self_data.get_structure();
  auto res =
      map([&](at::Tensor t) { return at::relu(t.unsqueeze(0)).squeeze(0); },
          structure);
  return at::detail::make_tensor<NestedTensorImpl>(
      torch::nested_tensor::NestedTensor(std::move(res)));
}

Tensor & NestedTensor_relu_(Tensor & self) {
  auto self_impl = get_nested_tensor_impl(self);
  auto self_data = self_impl->_data;
  if (self_data.is_contiguous()) {
    at::relu_(*self_data.get_buffer());
    return self;
  }
  auto structure = self_data.get_structure();
  apply([](at::Tensor& t) { at::relu_(t); }, structure);
  return self;
}

Tensor NestedTensor_dropout(const Tensor& input, double p, bool train) {
  auto self_impl = get_nested_tensor_impl(input);
  auto self_data = self_impl->_data;
  auto structure = self_data.get_structure();
  auto res =
      map([&](const at::Tensor t) { return at::dropout(t, p, train); }, structure);
  return at::detail::make_tensor<NestedTensorImpl>(
      torch::nested_tensor::NestedTensor(std::move(res)));
}

Tensor& NestedTensor_dropout_(Tensor& input, double p, bool train) {
  auto self_impl = get_nested_tensor_impl(input);
  auto self_data = self_impl->_data;
  auto structure = self_data.get_structure();
  apply([&](at::Tensor& t) { return at::dropout_(t, p, train); }, structure);
  return input;
}

Tensor NestedTensor_sum(const Tensor &self_, c10::optional<ScalarType> dtype) {
  auto self = get_nested_tensor_impl(self_)->_data;
  auto flat_structure =
      map([&dtype](at::Tensor tensor) { return at::sum(tensor, dtype); },
          self.get_structure());
  auto tensors = flatten(flat_structure).vec();
  if (tensors.size() == 0) {
    if (dtype) {
      return at::ones({0}, *dtype);
    }
    return at::ones({0});
  }
  auto all_tensor = at::stack(tensors);
  return at::sum(all_tensor, dtype);
}

void NestedTensor_backward(
    Tensor self,
    const Tensor& gradient = {},
    bool keep_graph = false,
    bool create_graph = false) {
  std::cout << "BACKWARD" << std::endl;
}

TORCH_LIBRARY_IMPL(aten, PrivateUse1_PreAutograd, m) {
  m.impl_UNBOXED("conv2d", NestedTensor_conv2d);
  m.impl_UNBOXED("batch_norm", NestedTensor_batch_norm);
  m.impl_UNBOXED("max_pool2d", NestedTensor_max_pool2d);
  m.impl_UNBOXED("relu", NestedTensor_relu);
  m.impl_UNBOXED("relu_", NestedTensor_relu_);
  m.impl_UNBOXED("dropout", NestedTensor_dropout);
  m.impl_UNBOXED("dropout_", NestedTensor_dropout_);
  m.impl_UNBOXED("sum", NestedTensor_sum);
  m.impl_UNBOXED("backward", NestedTensor_backward);
}
}
