#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <ethosu.hpp>


namespace py = pybind11;
using namespace EthosU;

py::dtype EthosUTypeToPyType(int ethosu_type) {
    switch (ethosu_type) {
        case TensorType_FLOAT32:
            return py::dtype("float32");
        case TensorType_FLOAT16:
            return py::dtype("float16");
        case TensorType_INT32:
            return py::dtype("int32");
        case TensorType_UINT8:
            return py::dtype("uint8");
        case TensorType_INT64:
            return py::dtype("int64");
        case TensorType_STRING:
            return py::dtype("str");
        case TensorType_BOOL:
            return py::dtype("bool");
        case TensorType_INT16:
            return py::dtype("int16");
        case TensorType_COMPLEX64:
            return py::dtype("complex64");
        case TensorType_INT8:
            return py::dtype("int8");
        case TensorType_FLOAT64:
            return py::dtype("float64");
    }
    return py::dtype("void");
}

class InterpreterWrapper {
public:
    InterpreterWrapper(const std::string &model) : 
       interpreter_(std::make_unique<Interpreter>(model)),
       inputInfo_(interpreter_->GetInputInfo()),
       outputInfo_(interpreter_->GetOutputInfo()) {}

    void SetInput(size_t i, py::array &in) {
       if (i < 0 || i >= inputInfo_.size()) {
           PyErr_Format(PyExc_ValueError,
                        "Cannot set input:"
                        " Invalid input index %d exceeds max index %lu",
                        i, inputInfo_.size() - 1);
           return;

       }
       if (!EthosUTypeToPyType(inputInfo_[i].type).is(in.dtype())) {
           PyErr_Format(PyExc_ValueError,
                        "Cannot set input:"
                        " Invalid input data type for input %d", i);
           return;

       }
       if ((size_t)in.ndim() != inputInfo_[i].shape.size()) {
           PyErr_Format(PyExc_ValueError,
                        "Cannot set input: Dimension mismatch."
                        " Got %d but expected %d for input %d.",
                        in.ndim(), inputInfo_[i].shape.size(), i);
           return;
       }
       for (int j = 0; j < in.ndim(); j++) {
           if (inputInfo_[i].shape[j] != (size_t)in.shape(j)) {
               PyErr_Format(PyExc_ValueError,
                            "Cannot set input: Dimension mismatch."
                            " Got %ld"
                            " but expected %d for dimension %d of input %d.",
                            in.shape(j), inputInfo_[i].shape[j], j, i);
              return;
           }
       }

       auto buffer = interpreter_->typed_input_buffer<int8_t>(i);
       auto data = in.data();
       size_t dims = in.nbytes();

       memcpy(buffer, data, dims);
       return;
    }

    py::array GetOutput(size_t i) {
       if (i < 0 || i >= outputInfo_.size()) {
           PyErr_Format(PyExc_ValueError,
                        "Cannot get output:"
                        " Invalid output index %d exceeds max index %lu",
                        i, outputInfo_.size() - 1);
           return py::array();
       }

       py::dtype type = EthosUTypeToPyType(outputInfo_[i].type);
       auto data = interpreter_->typed_output_buffer<int8_t>(i);
       auto shape = outputInfo_[i].shape;

       return py::array(type, shape, data);
    }

    void Invoke(int64_t timeoutNanos) {
        interpreter_->Invoke(timeoutNanos);
        return;
    }

    py::list GetInputDetails() {
        py::list details;
        for (size_t i = 0; i < inputInfo_.size(); i ++) {
            py::dict info;
            info["index"] = i;
            info["dtype"] = EthosUTypeToPyType(inputInfo_[i].type);
            info["ndim"] = inputInfo_[i].shape.size();
            py::list shape;
            for (size_t j = 0; j < inputInfo_[i].shape.size(); j++) {
                shape.append(inputInfo_[i].shape[j]);
            }
            info["shape"] = shape;
            details.append(info);
        }
        return details;
    }

    py::list GetOutputDetails() {
        py::list details;
        for (size_t i = 0; i < outputInfo_.size(); i ++) {
            py::dict info;
            info["index"] = i;
            info["dtype"] = EthosUTypeToPyType(outputInfo_[i].type);
            info["ndim"] = outputInfo_[i].shape.size();
            py::list shape;
            for (size_t j = 0; j < outputInfo_[i].shape.size(); j++) {
                shape.append(outputInfo_[i].shape[j]);
            }
            info["shape"] = shape;
            details.append(info);
        }
        return details;
    }

private:
    const std::unique_ptr<Interpreter> interpreter_;
    const std::vector<TensorInfo> inputInfo_;
    const std::vector<TensorInfo> outputInfo_;
};

PYBIND11_MODULE(ethosu, m) {
    m.doc() = "ethosu python API";

    py::class_<InterpreterWrapper>(m, "Interpreter")
        .def(py::init<const std::string &>())
        .def("set_input", &InterpreterWrapper::SetInput)
        .def("get_output", &InterpreterWrapper::GetOutput)
        .def("get_input_details", &InterpreterWrapper::GetInputDetails)
        .def("get_output_details", &InterpreterWrapper::GetOutputDetails)
        .def("invoke", &InterpreterWrapper::Invoke, py::arg("timeout_nanos") = 60000000000)
        .def("__repr__",
            [](const Interpreter &a) {
                return "<ethosu.Interpreter>";
            });
}
