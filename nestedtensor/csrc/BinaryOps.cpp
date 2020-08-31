#include <ATen/core/op_registration/op_registration.h>
#include <nestedtensor/csrc/nested_tensor_impl.h>
#include <nestedtensor/csrc/utils/nested_node_functions.h>
#include <torch/library.h>

// TODO: Non-NT argument support

namespace at {

using namespace torch::nested_tensor;

void check_binary_shape(const Tensor& self, const Tensor& other) {
  if (is_nested_tensor_impl(self, other)) {
    int64_t self_nested_dim = get_nested_tensor_impl(self)->nested_dim();
    int64_t other_nested_dim = get_nested_tensor_impl(other)->nested_dim();
    TORCH_CHECK(
        self_nested_dim == other_nested_dim,
        "self and other must be of the same nested dimension for NT binary op.");
  } else if (is_nested_tensor_impl(other)) {
    int64_t other_nested_dim = get_nested_tensor_impl(other)->nested_dim();
    TORCH_CHECK(
        self.dim() <= other.dim() - other_nested_dim,
        "tensor dimension of other must match or be greater than dimension of self.");
  } else if (is_nested_tensor_impl(self)) {
    int64_t self_nested_dim = get_nested_tensor_impl(self)->nested_dim();
    TORCH_CHECK(
        other.dim() <= self.dim() - self_nested_dim,
        "tensor dimension of self must match or be greater than dimension of other.");
  } else {
    TORCH_CHECK(false, "check_binary_shape can only be used in NT context.");
  }
}

template <Tensor& (*func)(Tensor&, const Tensor&)>
Tensor& NestedTensor_binary_(Tensor& self, const Tensor& other) {
  check_binary_shape(self, other);
  apply_nested_tensor(
      [](Tensor& tensor, const Tensor other) { func(tensor, other); },
      self,
      other);
  return self;
}

template <Tensor (*func)(const Tensor&, Scalar)>
Tensor NestedTensor_binary_scalar(const Tensor& self, Scalar other) {
  return autograd_map_nested_tensor(
      [&other](Tensor self) { return func(self, other); }, self);
}

template <Tensor (*func)(const Tensor&, const Tensor&)>
Tensor NestedTensor_binary(const Tensor& self, const Tensor& other) {
  check_binary_shape(self, other);
  return autograd_map_nested_tensor(
      [](Tensor s, Tensor o) { return func(s, o); }, self, other);
}

template <typename S, Tensor (*func)(const Tensor&, const Tensor&, S)>
Tensor NestedTensor_binary(const Tensor& self, const Tensor& other, S scalar) {
  check_binary_shape(self, other);
  return autograd_map_nested_tensor(
      [&scalar](Tensor self, Tensor other) {
        return func(self, other, scalar);
      },
      self,
      other);
}

template <Tensor& (*func)(Tensor&, const Tensor&, const Tensor&)>
Tensor& NestedTensor_binary_out(
    Tensor& result,
    const Tensor& self,
    const Tensor& other) {
  TORCH_CHECK(
      is_nested_tensor_impl(result),
      "NT binary out variant requires NT as result argument.");
  check_binary_shape(self, other);
  TORCH_CHECK(
      is_nested_tensor_impl(result, self, other),
      "binary_out doesn't support non-NT arguments.")
  apply_nested_tensor(
      [](Tensor& result, Tensor& tensor, Tensor& other) {
        return func(result, tensor, other);
      },
      result,
      self,
      other);
  return result;
}

Tensor& NestedTensor_sub_(Tensor& self, const Tensor& other, Scalar alpha) {
  check_binary_shape(self, other);
  if (is_nested_tensor_impl(self, other)) {
    torch_check_tensor_shape_matches(self, other);
    apply_nested_tensor(
        [&alpha](Tensor& tensor, Tensor& other) {
          at::native::sub_(tensor, other, alpha);
        },
        self,
        other);
    return self;
  }
  if (is_nested_tensor_impl(self)) {
    torch_check_tensor_shape_matches(self);
    apply_nested_tensor(
        [&other, &alpha](Tensor& self) {
          at::native::sub_(self, other, alpha);
        },
        self);
    return self;
  }
  torch_check_tensor_shape_matches(other);
  apply_nested_tensor(
      [&self, &alpha](Tensor& other) { at::native::sub_(self, other, alpha); },
      other);
  return self;
}

Tensor& NestedTensor_sub_out(
    Tensor& result,
    const Tensor& self,
    const Tensor& other,
    Scalar alpha) {
  TORCH_CHECK(
      is_nested_tensor_impl(result),
      "NT binary out variant requires NT as result argument.");
  check_binary_shape(self, other);
  is_nested_tensor_impl(result, self, other);
  apply_nested_tensor(
      [&alpha](Tensor& result, Tensor& tensor, Tensor& other) {
        return at::sub_out(result, tensor, other, alpha);
      },
      result,
      self,
      other);
  return result;
}

Tensor& NestedTensor_pow_out_1(
    Tensor& result,
    const Tensor& base,
    const Tensor& exp) {
  TORCH_CHECK(
      is_nested_tensor_impl(result),
      "NT binary out variant requires NT as result argument.");
  check_binary_shape(base, exp);
  if (is_nested_tensor_impl(result, base, exp)) {
    torch_check_tensor_shape_matches(result, base, exp);
    apply_nested_tensor(
        [](Tensor& result, Tensor& base, Tensor& exp) {
          at::pow_out(result, base, exp);
        },
        result,
        base,
        exp);
    return result;
  }
  if (is_nested_tensor_impl(result, base)) {
    torch_check_tensor_shape_matches(result, base);
    apply_nested_tensor(
        [&exp](Tensor& result, Tensor& base) {
          at::pow_out(result, base, exp);
        },
        result,
        base);
    return result;
  }
  TORCH_CHECK(
      is_nested_tensor_impl(result, exp),
      "At least one of base or exp needs to be a NestedTensor");
  torch_check_tensor_shape_matches(result, exp);
  apply_nested_tensor(
      [&exp](Tensor& result, Tensor& base) { at::pow_out(result, base, exp); },
      result,
      base);
  return result;
}

Tensor& NestedTensor_pow__1(Tensor& base, const Tensor& other) {
  check_binary_shape(base, other);
  return NestedTensor_pow_out_1(base, base, other);
}

Tensor& NestedTensor_pow_out_2(Tensor& result, const Tensor& base, Scalar exp) {
  apply_nested_tensor(
      [&exp](Tensor& result, Tensor& base) {
        return at::pow_out(result, base, exp);
      },
      result,
      base);
  return result;
}

Tensor NestedTensor_pow_2(const Tensor& base, Scalar exp) {
  return autograd_map_nested_tensor(
      [exp](Tensor base) { return at::pow(base, exp); }, base);
}

Tensor& NestedTensor_pow_out_3(Tensor& result, Scalar base, const Tensor& exp) {
  apply_nested_tensor(
      [&base](Tensor& result, Tensor& exp) {
        return at::pow_out(result, base, exp);
      },
      result,
      exp);
  return result;
}

Tensor NestedTensor_pow_3(Scalar base, const Tensor& exp) {
  return autograd_map_nested_tensor(
      [&base](Tensor exp) { return at::pow(base, exp); }, exp);
}

Tensor NestedTensor_add(const Tensor& self, const Tensor& other, Scalar alpha) {
  check_binary_shape(self, other);
  return autograd_map_nested_tensor(
      [&alpha](at::Tensor s, at::Tensor o) { return at::add(s, o, alpha); },
      self,
      other);
}

Tensor& NestedTensor_add_(Tensor& self, const Tensor& other, Scalar alpha) {
  check_binary_shape(self, other);
  apply_nested_tensor(
      [&](at::Tensor& s, at::Tensor o) { at::native::add_(s, o, alpha); }, self, other);
  return self;
}

#define BINARY_OP(NAME)                                                    \
  nt_impl(m, #NAME ".Tensor", NestedTensor_binary<at::NAME>);              \
  nt_impl(m, #NAME ".Scalar", NestedTensor_binary_scalar<at::NAME>);       \
  nt_impl(m, #NAME "_.Tensor", NestedTensor_binary_<at::native::NAME##_>); \
  nt_impl(m, #NAME ".out", NestedTensor_binary_out<at::NAME##_out>);

// XXX: We need to disable binary ops below autograd between NT and T, because
// in the backwards pass autograd/engine.cpp uses .sizes() which
// doesn't compare between NTs and Ts.
TORCH_LIBRARY_IMPL(aten, PrivateUse1_PreAutograd, m) {
  nt_impl(m, "add.Tensor", NestedTensor_add);
  nt_impl(m, "add_.Tensor", NestedTensor_add_);
  BINARY_OP(div)
  BINARY_OP(mul)
  BINARY_OP(remainder)

  // floor_divide has an inconsistent signature
  nt_impl(m, "floor_divide", NestedTensor_binary<at::floor_divide>);
  nt_impl(
      m,
      "floor_divide_.Tensor",
      NestedTensor_binary_<at::native::floor_divide_>);
  nt_impl(m, "floor_divide.out", NestedTensor_binary_out<at::floor_divide_out>);

  nt_impl(m, "eq.Tensor", NestedTensor_binary<at::eq>);
  nt_impl(m, "ne.Tensor", NestedTensor_binary<at::ne>);
  nt_impl(m, "eq.Scalar", NestedTensor_binary_scalar<at::eq>);
  nt_impl(m, "ne.Scalar", NestedTensor_binary_scalar<at::ne>);

  nt_impl(m, "atan2", NestedTensor_binary<at::atan2>);
  nt_impl(m, "atan2_", NestedTensor_binary_<at::native::atan2_>);
  nt_impl(m, "atan2.out", NestedTensor_binary_out<at::atan2_out>);

  nt_impl(m, "sub.Tensor", (NestedTensor_binary<Scalar, at::sub>));
  nt_impl(m, "sub_.Tensor", NestedTensor_sub_);
  nt_impl(m, "sub.out", NestedTensor_sub_out);

  nt_impl(m, "pow.Tensor_Tensor_out", NestedTensor_pow_out_1);
  nt_impl(m, "pow.Tensor_Tensor", NestedTensor_binary<at::pow>);
  nt_impl(m, "pow_.Tensor", NestedTensor_pow__1);
  nt_impl(m, "pow.Tensor_Scalar_out", NestedTensor_pow_out_2);
  nt_impl(m, "pow.Tensor_Scalar", NestedTensor_pow_2);
  nt_impl(m, "pow.Scalar_out", NestedTensor_pow_out_3);
  nt_impl(m, "pow.Scalar", NestedTensor_pow_3);
}
} // namespace at
