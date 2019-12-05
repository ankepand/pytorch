#include <torch/csrc/distributed/rpc/python_rpc_handler.h>
#include <c10/util/Logging.h>

namespace torch {
namespace distributed {
namespace rpc {

namespace {

py::object getFunction(const py::object& module, const char* name) {
  py::object fn = module.attr(name);
  TORCH_CHECK(
      py::isinstance<py::function>(fn),
      "attribute ",
      name,
      " is not a function");
  return fn;
}

} // namespace

PythonRpcHandler::PythonRpcHandler() {
  pybind11::gil_scoped_acquire ag;
  py::object module = py::module::import("torch.distributed.rpc.internal");
  pyRunFunction_ = getFunction(module, "_run_function");
  pyLoadReturnValue_ = getFunction(module, "_load_return_value");
  pySerialize_ = getFunction(module, "serialize");
  pyHandleException_ = getFunction(module, "_handle_exception");
}

void PythonRpcHandler::cleanup() {
  pybind11::gil_scoped_acquire ag;
  pyRunFunction_ = py::none();
  pyLoadReturnValue_ = py::none();
  pySerialize_ = py::none();
  pyHandleException_ = py::none();
}

PythonRpcHandler& PythonRpcHandler::getInstance() {
  static PythonRpcHandler handler;
  return handler;
}

std::vector<char> PythonRpcHandler::generatePythonUDFResult(
    const std::vector<char>& pickledPayload,
    const std::vector<torch::Tensor>& requestTensorTable,
    std::vector<torch::Tensor>& responseTensorTable) {
  pybind11::gil_scoped_acquire ag;
  auto pargs = py::bytes(pickledPayload.data(), pickledPayload.size());
  py::tuple pres = pySerialize_(pyRunFunction_(pargs, requestTensorTable));
  const auto& presStr = pres[0].cast<std::string>();
  responseTensorTable = pres[1].cast<std::vector<torch::Tensor>>();
  std::vector<char> payload(presStr.begin(), presStr.end());
  return payload;
}

py::object PythonRpcHandler::loadPythonUDFResult(
    const std::vector<char>& pickledPayload,
    const std::vector<torch::Tensor>& tensorTable) {
  pybind11::gil_scoped_acquire ag;
  auto pargs = py::bytes(pickledPayload.data(), pickledPayload.size());
  return pyLoadReturnValue_(pargs, tensorTable);
}

py::object PythonRpcHandler::runPythonUDF(
    const SerializedPyObj& serializedObj) {
  auto start = std::chrono::high_resolution_clock::now();
  pybind11::gil_scoped_acquire ag;
  auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::high_resolution_clock::now() - start);
  LOG(INFO) << "runPythonUDF: Took " << dur.count() << " us to acquire GIL.";
  // TODO: add this to metrics after https://github.com/pytorch/pytorch/pull/30833/ lands.
  return pyRunFunction_(
      py::bytes(serializedObj.payload_), serializedObj.tensors_);
}

SerializedPyObj PythonRpcHandler::serialize(const py::object& obj) {
  pybind11::gil_scoped_acquire ag;
  py::tuple t = pySerialize_(obj);
  return SerializedPyObj(
      t[0].cast<std::string>(), t[1].cast<std::vector<torch::Tensor>>());
}

py::object PythonRpcHandler::deserialize(const SerializedPyObj& serializedObj) {
  pybind11::gil_scoped_acquire ag;
  return pyLoadReturnValue_(
      py::bytes(serializedObj.payload_), serializedObj.tensors_);
}

void PythonRpcHandler::handleException(const py::object& obj) {
  pybind11::gil_scoped_acquire ag;
  pyHandleException_(obj);
}

} // namespace rpc
} // namespace distributed
} // namespace torch
