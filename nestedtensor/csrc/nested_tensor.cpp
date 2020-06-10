#include <ATen/ATen.h>
#include <nestedtensor/csrc/nested_tensor.h>
#include <nestedtensor/csrc/utils/nested_node_functions.h>

namespace torch {
namespace nested_tensor {

int64_t num_memory(c10::List<int64_t> size, c10::List<int64_t> stride) {
  // 0-dim Tensors have torch.Size of .size() 0, but carry 1 memory.
  // Empty 1-dim Tensors (torch.tensor([])) have torch.Size of .size() 1,
  // but carry 0 memory.
  if (size.size() == 0) {
    return 1;
  }
  return size[0] * stride[0];
}

std::vector<c10::optional<int64_t>> construct_size(const SizeNode& size_node) {
  if (size_node.is_leaf()) {
    std::vector<c10::optional<int64_t>> result;
    for (const auto& size : size_node.payload()) {
      result.push_back(size);
    }
    return result;
  }
  std::vector<c10::optional<int64_t>> result;
  result.push_back(size_node.degree());

  if (size_node.degree() > 0) {
    for (const auto& size : construct_size(size_node.children(0))) {
      result.push_back(size);
    }
    for (size_t i = 1; i < size_node.degree(); i++) {
      auto size_node_i = construct_size(size_node.children(i));
      for (size_t j = 1; j < result.size(); j++) {
        if (result[j] && ((*result[j]) != size_node_i[j - 1])) {
          result[j] = c10::nullopt;
        }
      }
    }
  }

  return result;
}

std::vector<c10::optional<int64_t>> NestedTensor::sizes() const {
  return construct_size(_nested_size);
}

c10::List<int64_t> _cont_stride(c10::List<int64_t> size) {
  std::vector<int64_t> stride(size.size());
  int64_t p = 1;
  size_t p_i = size.size();
  for (size_t i = 0; i < size.size(); i++) {
    p_i--;
    stride[p_i] = p;
    p *= size[p_i];
  }
  return c10::List<int64_t>(stride);
}

at::Tensor build_buffer(const TensorNode& structure) {
  c10::List<at::Tensor> tensors = flatten(structure);
  auto tensors_vec = tensors.vec();
  std::vector<at::Tensor> vectors;
  for (const auto& tensor : tensors_vec) {
    vectors.push_back(tensor.reshape({-1}));
  }
  if (vectors.size() == 0) {
    return at::ones({0});
  }
  return at::cat(vectors);
}

SizeNode infer_nested_size(const TensorNode structure) {
  return map(
      [](at::Tensor tensor) { return c10::List<int64_t>(tensor.sizes()); },
      structure);
}

SizeNode infer_nested_stride(const TensorNode structure) {
  return map(
      [](at::Tensor tensor) { return c10::List<int64_t>(tensor.strides()); },
      structure);
}

NestedTensor NestedTensor::contiguous() const {
  if (is_contiguous()) {
    return *this;
  }
  return NestedTensor(
      std::move(_buffer.contiguous()), _nested_size, _nested_stride);
}

at::Tensor NestedTensor::to_tensor() {
  // TODO: Not necessarily a view because of stack and reshape.
  std::vector<int64_t> new_size;
  for (const auto& si : sizes()) {
    if (!si) {
      // TODO: This assumes we'll extend to_tensor to also work with int64_t at
      // this level.
      throw std::out_of_range(
          "to_tensor()/to_tensor(0) only works if there is no None in size().");
    }
    new_size.push_back(*si);
  }
  return _buffer.reshape(at::IntArrayRef(new_size));
}

TensorNode _unbind_tensors(TensorNode structure) {
  std::vector<TensorNode> result_nodes;
  if (structure.is_leaf()) {
    for (at::Tensor tensor : structure.payload().unbind()) {
      result_nodes.emplace_back(TensorNode(std::move(tensor)));
    }
  } else {
    for (TensorNode child : structure.unbind()) {
      result_nodes.emplace_back(_unbind_tensors(child));
    }
  }
  return TensorNode(std::move(result_nodes));
}

NestedTensor NestedTensor::to_nested_tensor(c10::optional<int64_t> dim__) {
  int64_t dim_ = 0;
  if (dim__) {
    dim_ = *dim__;
  }
  int64_t dim = at::maybe_wrap_dim(dim_, this->dim());
  // if dim < nested_dim() the NestedTensor is already nested
  // up to the given dimension.
  if (dim >= nested_dim()) {
    TensorNode unbound = _unbind_tensors(get_structure());
    for (int64_t i = 0; i < (dim - nested_dim()); i++) {
      unbound = _unbind_tensors(unbound);
    }
    return NestedTensor(std::move(unbound));
  }
  return *this;
}

TensorNode build_structure(
    const at::Tensor& buffer,
    const SizeNode& nested_size,
    const SizeNode& nested_stride) {
  c10::List<int64_t> split_sizes = flatten(
      map([](c10::List<int64_t> a,
             c10::List<int64_t> b) { return num_memory(a, b); },
          nested_size,
          nested_stride));
  std::vector<int64_t> nonzero_split_sizes;
  for (size_t i = 0; i < split_sizes.size(); i++) {
    if (split_sizes[i] > 0) {
      nonzero_split_sizes.push_back(split_sizes[i]);
    }
  }
  std::vector<at::Tensor> buffers_;
  if (nonzero_split_sizes.size() > 0) {
    buffers_ =
        at::split_with_sizes(buffer, c10::IntArrayRef(nonzero_split_sizes), 0);
  }
  std::vector<at::Tensor> buffers;
  int64_t index = 0;
  for (size_t i = 0; i < split_sizes.size(); i++) {
    if (split_sizes[i] > 0) {
      buffers.push_back(buffers_[index]);
      index++;
    } else {
      buffers.push_back(at::empty({}, buffer.options()));
    }
  }
  TensorNode tmp = unflatten(nested_size, c10::List<at::Tensor>(buffers));
  return map(
      [](at::Tensor buffer,
         c10::List<int64_t> size,
         c10::List<int64_t> stride) {
        return at::as_strided(
            buffer,
            c10::IntArrayRef(size.vec()),
            c10::IntArrayRef(stride.vec()));
      },
      tmp,
      nested_size,
      nested_stride);
}

const TensorNode NestedTensor::get_structure() const {
  return build_structure(_buffer, _nested_size, _nested_stride);
}

NestedTensor::NestedTensor(const TensorNode structure)
    : _buffer(build_buffer(structure)),
      _nested_size(infer_nested_size(structure)) {}
//      _nested_stride(map(
//          [](at::Tensor tensor) {
//            return c10::List<int64_t>(tensor.strides());
//          },
//          structure)) {}

// NOTE: It is assumed that structure is a tree of views
// of buffer.
// TODO: Add an explicit test for debug purposes.
NestedTensor::NestedTensor(at::Tensor&& buffer, const TensorNode structure)
    : _buffer(buffer), _nested_size(infer_nested_size(structure)) {}
//      _nested_stride(map(
//          [](at::Tensor tensor) {
//            return c10::List<int64_t>(tensor.strides());
//          },
//          structure)) {}

NestedTensor::NestedTensor(at::Tensor&& buffer, SizeNode nested_size)
    : _buffer(buffer),
      _nested_size(nested_size),
      _nested_stride(_cont_stride(nested_size)) {}

NestedTensor::NestedTensor(
    at::Tensor&& buffer,
    SizeNode nested_size,
    SizeNode nested_stride)
    : _buffer(buffer),
      _nested_size(nested_size),
      _nested_stride(nested_stride) {}

// torch.Tensor methods
NestedTensor NestedTensor::copy_(
    const NestedTensor& source,
    bool non_blocking) {
  TORCH_CHECK(
      shape_matches(nested_size(), source.nested_size()),
      "self and source don't match in shape");
  _buffer.copy_(source.get_buffer());
  return *this;
}

inline TensorNode _squeeze_nested_dim(TensorNode structure, int64_t dim) {
  if (dim == 0) {
    return structure.children(0);
  }
  return TensorNode(_squeeze_nested_dim(structure, dim - 1));
}

NestedTensor NestedTensor::squeeze_(c10::optional<int64_t> dim_) {
  if (!dim_) {
    // TODO: First dimension is always ignored.
    // We could decide to return a Tensor if the 0th
    // dimension can be squeezed.
    auto init_sizes = sizes();
    for (size_t i = 0; i < init_sizes.size() - 1; i++) {
      int64_t index = init_sizes.size() - i - 1;
      c10::optional<int64_t> s = init_sizes[index];
      if (s && ((*s) == 1)) {
        this->squeeze_(index);
      }
    }
    return *this;
  }
  int64_t dim = at::maybe_wrap_dim(*dim_, this->dim());
  TORCH_CHECK(dim > 0, "Cannot squeeze first dimension.");
  TORCH_CHECK(
      ((sizes()[dim]) && ((*(sizes()[dim])) == 1)),
      "Given dimension is either undefined or not a singleton.");
  if (dim < this->nested_dim()) {
    auto structure = _squeeze_nested_dim(get_structure(), dim);
    _nested_size = infer_nested_size(structure);
    _nested_stride = infer_nested_stride(structure);
  } else {
    int64_t height = _nested_size.height();
    auto structure =
        map([dim, height](
                at::Tensor tensor) { return tensor.squeeze(dim - height); },
            get_structure());
    _nested_size = infer_nested_size(structure);
    _nested_stride = infer_nested_stride(structure);
  }
  return *this;
}

} // namespace nested_tensor
} // namespace torch
