#include <nestedtensor/csrc/creation.h>
#include <nestedtensor/csrc/jit_list_apply.h>
#include <nestedtensor/csrc/nested_tensor_impl.h>
#include <nestedtensor/csrc/python_functions.h>
#include <nestedtensor/csrc/unary.h>
#include <nestedtensor/csrc/utils/nested_node_functions.h>
#include <nestedtensor/csrc/utils/python_nested_node.h>
#include <torch/extension.h>

// TODO: Add a field such as is_empty to _NestedNode?
// TODO: Remove Variable-only _NestedNodes and replace them with TensorList?
// TODO: Abstract the common recursive patterns.
// TODO: NestedSize C++ object
// TODO: Align NestedTensor and Tensor C++ API

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

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
  register_python_nested_node(m);

  // NOTE: Never forget about pybind return value policies
  // since you can expect transparent changes to the constiuents
  // via unbind.
  auto c = py::class_<THPNestedTensor>(m, "NestedTensor");
  c.def_property_readonly("dtype", &THPNestedTensor::getDtype)
      .def_property_readonly("layout", &THPNestedTensor::getLayout)
      .def_property_readonly("device", &THPNestedTensor::getDevice)
      .def_property_readonly("requires_grad", &THPNestedTensor::requires_grad)
      .def("__len__", &THPNestedTensor::len)
      .def("element_size", &THPNestedTensor::element_size)
      .def("nested_size",
          torch::wrap_pybind_function([](THPNestedTensor self, c10::optional<int64_t> dim) {
            return self.nested_size(dim);
          }))
      .def("nested_stride",
          torch::wrap_pybind_function([](THPNestedTensor self, c10::optional<int64_t> dim) {
            return self.nested_stride(dim);
          }))
      .def("__getitem__", py::overload_cast<int64_t>(&THPNestedTensor::getitem))
#if (PYBIND11_VERSION_MAJOR == 2 && PYBIND11_VERSION_MINOR >= 4)
      .def(
          "__getitem__",
          py::overload_cast<py::slice>(&THPNestedTensor::getitem))
#endif
      .def(
          "unbind",
          torch::wrap_pybind_function([](THPNestedTensor self, int64_t dim) {
            return self.unbind(dim);
          }))
      .def("size", &THPNestedTensor::size)
      .def("requires_grad_", &THPNestedTensor::requires_grad_)
      .def("numel", &THPNestedTensor::numel)
      .def_property_readonly("grad", &THPNestedTensor::grad)
      .def("detach", &THPNestedTensor::detach)
      .def("dim", &THPNestedTensor::dim)
      .def("pin_memory", &THPNestedTensor::pin_memory)
      .def("nested_dim", &THPNestedTensor::nested_dim)
      .def("is_pinned", &THPNestedTensor::is_pinned)
      .def("is_contiguous", &THPNestedTensor::is_contiguous)
      .def("contiguous", &THPNestedTensor::contiguous)
      .def("get_buffer", &THPNestedTensor::get_buffer)
      .def(
          "to_tensor",
          torch::wrap_pybind_function(
              [](THPNestedTensor self, c10::optional<int64_t> dim) {
                return self.to_tensor(dim);
              }))
      .def(
          "to_nested_tensor",
          torch::wrap_pybind_function(
              [](THPNestedTensor self, c10::optional<int64_t> dim) {
                return self.to_nested_tensor(dim);
              }))
      .def("to_list", &THPNestedTensor::to_list)
      .def("to_tuple", &THPNestedTensor::to_tuple)
      .def("__str__", &THPNestedTensor::str)
      .def("__repr__", &THPNestedTensor::str);

  add_unary_functions(m, c);
  add_functions(m, c);

  // NOTE: This is a private function until it is feature complete
  m.def("_jit_tensorwise", &torch::nested_tensor::jit_tensorwise);
  m.def("as_nested_tensor", &torch::nested_tensor::as_nested_tensor);
  m.def("as_nested_tensor_impl", &torch::nested_tensor::as_nested_tensor_impl);
  m.def("is_nested_tensor_impl", [](at::Tensor tensor) {
    return tensor.unsafeGetTensorImpl()->key_set().has(at::NestedTensorKey);
  });
  m.def("nested_dim", [](at::Tensor tensor) {
    if (!tensor.unsafeGetTensorImpl()->key_set().has(at::NestedTensorKey)) {
      throw std::runtime_error("Function requires NestedTensorImpl");
    }
    return static_cast<at::NestedTensorImpl*>(tensor.unsafeGetTensorImpl())
        ->_data.nested_dim();
  });

  m.def(
      "make_nested_tensor_impl", [](std::vector<at::Tensor> tensors) {
        std::vector<TensorNode> tensor_nodes;
        for (size_t i = 0; i < tensors.size(); i++) {
          tensor_nodes.push_back(TensorNode(std::move(tensors[i])));
        }
        return at::detail::make_tensor<at::NestedTensorImpl>(
            NestedTensor(TensorNode(std::move(tensor_nodes))));
      });
}
