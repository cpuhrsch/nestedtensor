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

std::tuple<std::vector<int64_t>, std::vector<int64_t>> make_split_dims(
    const Tensor& self,
    c10::ArrayRef<int64_t> dims) {
  auto nt_impl = get_nested_tensor_impl(self);
  int64_t nested_dim = nt_impl->nested_dim();
  std::vector<int64_t> tensordims;
  std::vector<int64_t> nesteddims;
  for (size_t i = 0; i < dims.size(); i++) {
    int64_t dim = maybe_wrap_dim(dims[i], self.dim());
    if (dim < nested_dim) {
      nesteddims.push_back(dim);
    } else {
      tensordims.push_back(dim - nested_dim);
    }
  }
  return std::make_tuple(tensordims, nesteddims);
}

template <typename F>
Tensor NestedTensor_func_dim(
    F& fn,
    const Tensor& self,
    c10::ArrayRef<int64_t> dims,
    bool keepdims,
    c10::optional<ScalarType> dtype) {
  std::vector<int64_t> tensordims;
  std::vector<int64_t> nesteddims;
  std::tie(tensordims, nesteddims) = make_split_dims(self, dims);
  at::Tensor output = self;
  if (tensordims.size() > 0) {
    output = map_nested_tensor(
        [fn, tensordims, keepdims, dtype](at::Tensor tensor) {
          return fn(
              tensor, c10::ArrayRef<int64_t>(tensordims), keepdims, dtype);
        },
        output);
  }
  if (nesteddims.size() > 0) {
    auto opt_sizes = get_opt_sizes(output);
    for (auto opt_size : opt_sizes) {
      TORCH_CHECK(
          opt_size,
          "Current shape doesn't support reduction across nested dimension. Please open a feature request https://t.ly/62F6.");
    }
    auto new_nested_size = get_nested_size(output);
    for (size_t i = nesteddims.size(); i > 0; i--) {
      new_nested_size = squeeze(new_nested_size, nesteddims[i - 1], keepdims);
    }
    auto tmp =
        fn(NestedTensor_to_tensor(output, c10::nullopt),
           IntArrayRef(nesteddims),
           keepdims,
           dtype);
    return wrap_buffer(tmp.reshape({-1}), new_nested_size);
  }
  return output;
}

Tensor NestedTensor_sum_dim(
    const Tensor& self,
    c10::ArrayRef<int64_t> dims,
    bool keepdims,
    c10::optional<ScalarType> dtype) {
  auto my_sum = [](const Tensor& self,
                   IntArrayRef dims,
                   bool keepdims,
                   c10::optional<ScalarType> dtype) {
    return at::sum(self, dims, keepdims, dtype);
  };
  return NestedTensor_func_dim<decltype(my_sum)>(
      my_sum, self, dims, keepdims, dtype);
}

Tensor NestedTensor_mean_dim(
    const Tensor& self,
    c10::ArrayRef<int64_t> dims,
    bool keepdims,
    c10::optional<ScalarType> dtype) {
  auto my_mean = [](const Tensor& self,
                    IntArrayRef dims,
                    bool keepdims,
                    c10::optional<ScalarType> dtype) {
    return at::mean(self, dims, keepdims, dtype);
  };
  return NestedTensor_func_dim<decltype(my_mean)>(
      my_mean, self, dims, keepdims, dtype);
}

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
  return at::sum(self, dtype).div_(torch::tensor(self.numel()));
}

std::tuple<Tensor, Tensor, Tensor> _merge_m2(
    Tensor m2_tensor,
    Tensor mean_tensor,
    Tensor numel) {
  TORCH_CHECK(
      m2_tensor.dim() == 1 && mean_tensor.dim() == 1 && numel.dim() == 1,
      "merge tensors aren't of dimension 1.");
  if (m2_tensor.size(0) <= 1) {
    return std::make_tuple(m2_tensor, mean_tensor, numel);
  }
  int64_t start = 0;
  int64_t mid = m2_tensor.size(0) / 2;
  int64_t end = mid * 2;
  at::Tensor numel_0 = at::slice(numel, 0, start, mid);
  at::Tensor numel_1 = at::slice(numel, 0, mid, end);
  at::Tensor mean_0 = at::slice(mean_tensor, 0, start, mid);
  at::Tensor mean_1 = at::slice(mean_tensor, 0, mid, end);
  at::Tensor m2_0 = at::slice(m2_tensor, 0, start, mid);
  at::Tensor m2_1 = at::slice(m2_tensor, 0, mid, end);
  at::Tensor numel_prod = numel_0 * numel_1;
  at::Tensor numel_sum = numel_0 + numel_1;
  at::Tensor delta = mean_0 - mean_1;
  at::Tensor output_m2 =
      (m2_0 + m2_1) + delta * delta * (numel_prod / numel_sum);
  at::Tensor new_mean =
      (numel_0 / numel_sum) * mean_0 + (numel_1 / numel_sum) * mean_1;
  if (end < m2_tensor.size(0)) {
    output_m2 = torch::cat({output_m2, at::slice(m2_tensor, 0, end)});
    new_mean = torch::cat({new_mean, at::slice(mean_tensor, 0, end)});
    numel_sum = torch::cat({numel_sum, at::slice(numel, 0, end)});
  }
  return _merge_m2(output_m2, new_mean, numel_sum);
}

Tensor NestedTensor_var(const Tensor& self, bool unbiased) {
  auto m2_tensors = flatten(map(
      [](at::Tensor tensor) {
        return ((tensor - at::mean(tensor, c10::nullopt)) *
                (tensor - at::mean(tensor, c10::nullopt)))
            .sum();
      },
      get_nested_tensor_structure(self)));
  if (m2_tensors.size() == 0) {
    return at::ones({0});
  }
  auto mean_tensors = flatten(
      map([](at::Tensor tensor) { return at::mean(tensor, c10::nullopt); },
          get_nested_tensor_structure(self)));
  at::Tensor numel =
      torch::tensor(flatten(
                        map([](at::Tensor tensor) { return tensor.numel(); },
                            get_nested_tensor_structure(self))))
          .reshape({-1});
  at::Tensor m2_tensor = at::stack(m2_tensors).reshape({-1});
  at::Tensor mean_tensor = at::stack(mean_tensors).reshape({-1});
  std::tie(m2_tensor, mean_tensor, numel) =
      _merge_m2(m2_tensor, mean_tensor, numel);
  TORCH_CHECK(m2_tensor.size(0) == 1, "output size wrong.");
  if (unbiased) {
    return m2_tensor[0] / (numel[0] - 1);
  }
  return m2_tensor[0] / numel[0];
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

// Sums `tensor` repeatedly to produce a tensor of shape `shape`.
// Precondition: is_expandable_to(shape, tensor.sizes()) must be true
Tensor NestedTensor_sum_to(const Tensor& tensor_, IntArrayRef shape) {
  if (shape.size() == 0) {
    return tensor_.sum();
  }
  auto nt_impl = get_nested_tensor_impl(tensor_);

  TORCH_CHECK(
      tensor_.dim() >= nt_impl->nested_dim() + shape.size(),
      "sum_to only supported along tensor dimensions.");

  at::Tensor tensor = tensor_;

  std::vector<int64_t> reduce_dims;
  const int64_t leading_dims = tensor.dim() - shape.size();
  for (int64_t i = 0; i < leading_dims; ++i) {
    reduce_dims.push_back(i);
  }
  if (!reduce_dims.empty()) {
    tensor = tensor.sum(reduce_dims, /*keepdim=*/true);
  }
  reduce_dims.clear();
  const at::IntArrayRef sizes = tensor.sizes();
  for (int64_t i = leading_dims; i < static_cast<int64_t>(sizes.size()); ++i) {
    if (shape[i - leading_dims] == 1 && sizes[i] != 1) {
      reduce_dims.push_back(i);
    }
  }
  if (!reduce_dims.empty()) {
    tensor = tensor.sum(reduce_dims, /*keepdim=*/true);
  }
  return leading_dims > 0 ? tensor.view(shape) : tensor;
}

TORCH_LIBRARY_IMPL(aten, NestedTensor, m) {
  nt_impl(m, "sum", NestedTensor_sum);
  nt_impl(m, "sum.dim_IntList", NestedTensor_sum_dim);
  nt_impl(m, "mean", NestedTensor_mean);
  nt_impl(m, "mean.dim", NestedTensor_mean_dim);
  nt_impl(m, "var", NestedTensor_var);
  nt_impl(m, "prod", NestedTensor_prod);
  nt_impl(m, "cumsum", NestedTensor_cumsum);
  nt_impl(m, "sum_to", NestedTensor_sum_to);
}

} // namespace at
