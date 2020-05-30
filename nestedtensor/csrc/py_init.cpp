#include <nestedtensor/csrc/creation.h>
#include <nestedtensor/csrc/nested_tensor_impl.h>
#include <nestedtensor/csrc/utils/nested_node_functions.h>
#include <nestedtensor/csrc/utils/nested_node.h>
#include <nestedtensor/csrc/python_functions.h>
#include <torch/csrc/Size.h>
#include <torch/extension.h>

// NOTE: A NestedTensor without any constituents, i.e.
// nested_tensor([]) is of dimension 1 because
// tensor([]) is of dimension 1, but it is also
// of nested_dim 1, since there are no constituents
// and thus we choose that to imply that the constituents
// tensor dimension is 0.
// If depth is 0, it means that the current structure
// is already a leaf, i.e. has no children.

namespace py = pybind11;

using namespace torch::nested_tensor;
using namespace at;

namespace torch {
namespace nested_tensor {
namespace {

static auto registry =
    torch::RegisterOperators()
      .op("nestedtensor::is_nested_tensor_impl", [](Tensor tensor) {
        return tensor.unsafeGetTensorImpl()->key_set().has(NestedTensorKey);
      })
      .op("nestedtensor::nested_dim", [](Tensor tensor) {
        return get_nested_tensor_impl(tensor)->_data.nested_dim();
      })
      .op("nestedtensor::to_nested_tensor",
              [](Tensor tensor, c10::optional<int64_t> dim) {
                auto nt = get_nested_tensor(tensor);
                return wrap_nested_tensor(nt.to_nested_tensor(dim));
              })
      .op("nestedtensor::grad", 
            [](Tensor tensor) {
              auto nt = get_nested_tensor(tensor);
              return wrap_nested_tensor(nt.grad());
            })
      .op("nestedtensor::requires_grad", 
            [](Tensor tensor) {
              auto nt = get_nested_tensor(tensor);
              return nt.requires_grad();
            })
      .op("nestedtensor::requires_grad_",
            [](Tensor tensor, bool requires_grad) {
            auto nt = get_nested_tensor(tensor);
            return wrap_nested_tensor(nt.requires_grad_(requires_grad));
          })
      .op("nestedtensor::backward",
          [](Tensor tensor,
             Tensor gradient,
             bool retain_graph,
             bool create_graph) {
            auto nt = get_nested_tensor(tensor);
            nt.backward(get_nested_tensor(gradient), retain_graph, create_graph);
          })
      .op("nestedtensor::sizes", [](Tensor tensor) {
        return get_nested_tensor(tensor).sizes();
      })
      .op("nestedtensor::len", [](Tensor self) {
        return (int64_t)(get_nested_tensor(self).get_structure().degree());
      })
      .op("nestedtensor::to_tensor",
        [](Tensor tensor, c10::optional<int64_t> dim) {
          return NestedTensor_to_tensor(tensor, dim);
        })
      ;

}
} // namespace nested_tensor
} // namespace torch

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
  // NOTE: Never forget about pybind return value policies
  // since you can expect transparent changes to the constiuents
  // via unbind.

  m.def("nested_tensor_impl", &torch::nested_tensor::nested_tensor_impl);
  m.def("str", [](Tensor tensor) {
    auto impl_data = get_nested_tensor_impl(tensor)->_data;
    auto node = impl_data.get_structure();
    return NestedNode___str__(
        node,
        "nested_tensor",
        [](c10::IValue payload, const std::string& tabs) {
          std::vector<std::string> tokens = split_str(
              THPUtils_unpackString(
                  PyObject_Str(THPVariable_Wrap(payload.toTensor()))),
              "\n");
          std::string result;
          for (size_t i = 0; i < tokens.size(); i++) {
            result = result + tabs + tokens[i];
            if (i < tokens.size() - 1) {
              result = result + "\n";
            }
          }
          return result;
        });
  });
  // Need to overwrite because
  // https://github.com/pytorch/pytorch/blob/09660896c0dd2bec888857300a7be9edb52dd05d/aten/src/ATen/TensorIndexing.h#L480
  // requires sizes() for non Tensor-shape compliant NestedTensors
  // and can't be overwritten since it's not a native function.
  // TODO: Advanced indexing
  // TODO: Tensor-wise select
  // TODO: Tuple support
  m.def("get_item", [](Tensor tensor, int64_t key) {
    return unbind(tensor, 0)[key];
  });
#if (PYBIND11_VERSION_MAJOR == 2 && PYBIND11_VERSION_MINOR >= 4)
  m.def("get_item", [](Tensor tensor, py::slice key) {
    py::list unbound = py::cast(unbind(tensor, 0));
    return unbound[key];
  });
#endif

  m.def("make_nested_tensor_impl", [](std::vector<Tensor> tensors) {
    std::vector<TensorNode> tensor_nodes;
    for (size_t i = 0; i < tensors.size(); i++) {
      tensor_nodes.push_back(TensorNode(std::move(tensors[i])));
    }
    return wrap_tensor_node(std::move(tensor_nodes));
  });

  add_functions(m);
}
