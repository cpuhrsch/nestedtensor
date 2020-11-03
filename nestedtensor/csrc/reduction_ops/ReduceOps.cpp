#include <ATen/core/op_registration/op_registration.h>
#include <nestedtensor/csrc/nested_tensor_impl.h>
#include <torch/library.h>
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>

namespace at {

using namespace torch::nested_tensor;

Tensor NestedTensor_cumsum(
    const Tensor& self,
    int64_t dim,
    c10::optional<ScalarType> dtype) {
  auto nt_impl = get_nested_tensor_impl(self);
  int64_t nested_dim = nt_impl->nested_dim();
  dim = maybe_wrap_dim(dim, nt_impl->dim());
  TORCH_CHECK(
      dim >= nested_dim, "cumsum of nested dimensions is not implemented yet.");
  return map_nested_tensor(
      [nested_dim, dim](at::Tensor tensor) {
        return at::cumsum(tensor, dim - nested_dim);
      },
      self);
}

#define REDUCE_DIM_LIST_FUNC(NAME, FUNC, MSG)                                                                                     \
  Tensor NestedTensor_##NAME(                                                                                                     \
      const Tensor& self,                                                                                                         \
      c10::ArrayRef<int64_t> dims,                                                                                                \
      bool keepdims,                                                                                                              \
      c10::optional<ScalarType> dtype) {                                                                                          \
    auto nt_impl = get_nested_tensor_impl(self);                                                                                  \
    int64_t nested_dim = nt_impl->nested_dim();                                                                                   \
    std::vector<int64_t> newdims;                                                                                                 \
    for (auto dim : dims) {                                                                                                       \
      dim = maybe_wrap_dim(dim, nt_impl->dim());                                                                                  \
      newdims.push_back(dim);                                                                                                     \
    }                                                                                                                             \
    std::sort(newdims.begin(), newdims.end(), std::greater<int64_t>());                                                           \
    std::vector<int64_t> tensordims;                                                                                              \
    std::vector<int64_t> nesteddims;                                                                                              \
    at::Tensor output = self;                                                                                                     \
    for (int64_t dim : newdims) {                                                                                                 \
      if (dim < nested_dim) {                                                                                                     \
        nesteddims.push_back(dim);                                                                                                \
      } else {                                                                                                                    \
        tensordims.push_back(dim - nested_dim);                                                                                   \
      }                                                                                                                           \
    }                                                                                                                             \
    if (tensordims.size() > 0) {                                                                                                  \
      output = map_nested_tensor(                                                                                                 \
          [tensordims, keepdims](at::Tensor tensor) {                                                                             \
            return FUNC(tensor, c10::ArrayRef<int64_t>(tensordims), keepdims);                                                    \
          },                                                                                                                      \
          output);                                                                                                                \
    }                                                                                                                             \
    if (nesteddims.size() > 0) {                                                                                                  \
      nt_impl = get_nested_tensor_impl(output);                                                                                   \
      std::vector<c10::optional<int64_t>> opt_sizes = nt_impl->opt_sizes();                                                       \
      for (auto opt_size : opt_sizes) {                                                                                           \
        TORCH_CHECK(                                                                                                              \
            opt_size,                                                                                                             \
            "Current shape doesn't support reduction across nested dimension. Please open a feature request https://t.ly/62F6."); \
      }                                                                                                                           \
      at::Tensor data_tensor = NestedTensor_to_tensor(output, c10::nullopt);                                                      \
      data_tensor =                                                                                                               \
          FUNC(data_tensor, c10::ArrayRef<int64_t>(nesteddims), keepdims);                                                        \
      auto new_nested_size = nt_impl->nested_size();                                                                              \
      for (auto dim : nesteddims) {                                                                                               \
        new_nested_size = squeeze(new_nested_size, dim);                                                                          \
      }                                                                                                                           \
      return wrap_buffer(data_tensor.reshape({-1}), new_nested_size);                                                             \
    }                                                                                                                             \
    return output;                                                                                                                \
  }

REDUCE_DIM_LIST_FUNC(mean_dim, at::mean, "mean");
REDUCE_DIM_LIST_FUNC(sum_dim, at::sum, "sum");
#undef REDUCE_DIM_LIST_FUNC

Tensor NestedTensor_sum(const Tensor& self, c10::optional<ScalarType> dtype) {
  auto tensors = flatten(
      map([&dtype](at::Tensor tensor) { return at::sum(tensor, dtype); },
          get_nested_tensor_structure(self)));
  if (tensors.size() == 0) {
    if (dtype) {
      return at::ones({0}, *dtype);
    }
    return at::ones({0});
  }
  auto all_tensor = at::stack(tensors);
  return at::sum(all_tensor, dtype);
}

Tensor NestedTensor_mean(const Tensor& self, c10::optional<ScalarType> dtype) {
  auto tensors = flatten(
      map([&dtype](at::Tensor tensor) { return at::mean(tensor, dtype); },
          get_nested_tensor_structure(self)));
  if (tensors.size() == 0) {
    if (dtype) {
      return at::ones({0}, *dtype);
    }
    return at::ones({0});
  }
  auto all_tensor = at::stack(tensors);
  return at::mean(all_tensor, dtype);
}

Tensor NestedTensor_prod(const Tensor& self, c10::optional<ScalarType> dtype) {
  auto tensors = flatten(
      map([&dtype](at::Tensor tensor) { return at::prod(tensor, dtype); },
          get_nested_tensor_structure(self)));
  if (tensors.size() == 0) {
    if (dtype) {
      return at::ones({1}, *dtype);
    }
    return at::ones({1});
  }
  auto all_tensor = at::stack(tensors);
  return at::prod(all_tensor, dtype);
}

TORCH_LIBRARY_IMPL(aten, PrivateUse1, m) {
  nt_impl(m, "sum", NestedTensor_sum);
  nt_impl(m, "sum.dim_IntList", NestedTensor_sum_dim);
  nt_impl(m, "mean.dim", NestedTensor_mean_dim);
  nt_impl(m, "mean", NestedTensor_mean);
  nt_impl(m, "prod", NestedTensor_prod);
  nt_impl(m, "cumsum", NestedTensor_cumsum);
}

} // namespace at
