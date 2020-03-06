#include <python_args.h>
#include <torch/csrc/utils/python_arg_parser.h>

namespace torch {
namespace python_args {

at::Scalar to_scalar(py::object obj) {
  TORCH_CHECK(
      !jit::tracer::isTracing(), "nestedtensor doesn't support tracing.");
  PyObject* cobj = obj.ptr();

  // Zero-dim tensors are converted to Scalars as-is. Note this doesn't
  // currently handle most NumPy scalar types except np.float64.
  if (THPVariable_Check(cobj)) {
    return ((THPVariable*)cobj)->cdata.item();
  }

  if (THPUtils_checkLong(cobj)) {
    return at::Scalar(static_cast<int64_t>(THPUtils_unpackLong(cobj)));
  }

  if (PyBool_Check(cobj)) {
    return at::Scalar(THPUtils_unpackBool(cobj));
  }

  if (PyComplex_Check(cobj)) {
    return at::Scalar(THPUtils_unpackComplexDouble(cobj));
  }
  return at::Scalar(THPUtils_unpackDouble(cobj));
}

at::ScalarType to_scalar_type(py::object obj) {
  if (THPDtype_Check(obj.ptr())) {
    auto dtype = reinterpret_cast<THPDtype*>(obj.ptr());
    return dtype->scalar_type;
  }
  if (obj.ptr() == (PyObject*)(&PyFloat_Type)) {
    return c10::ScalarType::Float;
  }
  if (obj.ptr() == (PyObject*)(&PyBool_Type)) {
    return c10::ScalarType::Bool;
  }
  if (obj.ptr() == (PyObject*)(&PyLong_Type)) {
    return c10::ScalarType::Long;
  }
  TORCH_CHECK(false, "Cannot convert given python object ", obj, " to ScalarType");
}

} // namespace python_args
} // namespace torch
