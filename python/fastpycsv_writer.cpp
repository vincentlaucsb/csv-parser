#include "fastpycsv_bindings.hpp"

#include <fstream>

namespace {
    struct PythonTextOutput {
        nb::object file;

        explicit PythonTextOutput(nb::object output)
            : file(std::move(output)) {}

        void write(const char* data, std::streamsize size) {
            nb::object result = this->file.attr("write")(nb::str(data, static_cast<size_t>(size)));
            (void)result;
        }

        void flush() {
            if (PyObject_HasAttrString(this->file.ptr(), "flush")) {
                nb::object result = this->file.attr("flush")();
                (void)result;
            }
        }
    };

    template<class OutputStream>
    struct PythonRowWriter {
        csv::CSVWriter<OutputStream> writer;
        nb::object fieldnames;
        bool wrote_header = false;
        bool write_header = true;

        PythonRowWriter(
            OutputStream& out,
            nb::object selected_fieldnames,
            bool should_write_header,
            bool quote_minimal
        ) : writer(csv::make_csv_writer(out, quote_minimal)),
            fieldnames(std::move(selected_fieldnames)),
            write_header(should_write_header) {
            this->writer.set_auto_flush(false);
        }

        void write_all(nb::handle rows) {
            nb::object iterator = this->iter(rows);
            if (!this->fieldnames.is_none() && this->write_header) {
                this->write_header_row();
            }

            while (true) {
                PyObject* raw_row = PyIter_Next(iterator.ptr());
                if (raw_row == nullptr) {
                    if (PyErr_Occurred()) {
                        throw nb::python_error();
                    }
                    break;
                }

                nb::object row = nb::steal<nb::object>(raw_row);
                this->write_one(row);
            }

            this->writer.flush();
        }

    private:
        nb::object iter(nb::handle object) const {
            PyObject* raw_iterator = PyObject_GetIter(object.ptr());
            if (raw_iterator == nullptr) {
                throw nb::type_error("rows must be an iterable of row objects");
            }
            return nb::steal<nb::object>(raw_iterator);
        }

        nb::object sequence(nb::handle object, const char* message) const {
            PyObject* raw_sequence = PySequence_Fast(object.ptr(), message);
            if (raw_sequence == nullptr) {
                throw nb::python_error();
            }
            return nb::steal<nb::object>(raw_sequence);
        }

        nb::object mapping_keys(nb::handle mapping) const {
            PyObject* raw_keys = PyMapping_Keys(mapping.ptr());
            if (raw_keys == nullptr) {
                PyErr_Clear();
                return nb::none();
            }
            return nb::steal<nb::object>(raw_keys);
        }

        bool is_mapping_row(nb::handle row) const {
            if (PyDict_Check(row.ptr())) {
                return true;
            }

            return !this->fieldnames.is_none() && PyMapping_Check(row.ptr()) != 0;
        }

        void write_one(nb::handle row) {
            if (this->fieldnames.is_none()) {
                nb::object keys = this->mapping_keys(row);
                if (!keys.is_none()) {
                    this->fieldnames = std::move(keys);
                }
            }

            this->write_header_row();

            if (this->is_mapping_row(row)) {
                this->writer << this->mapping_to_strings(row);
                return;
            }

            this->writer << this->sequence_to_strings(row);
        }

        void write_header_row() {
            if (this->fieldnames.is_none() || this->wrote_header || !this->write_header) {
                return;
            }

            this->writer << this->sequence_to_strings(this->fieldnames);
            this->wrote_header = true;
        }

        std::vector<std::string> sequence_to_strings(nb::handle object) const {
            nb::object fields = this->sequence(object, "rows must contain iterable row objects");
            PyObject** items = PySequence_Fast_ITEMS(fields.ptr());
            const Py_ssize_t size = PySequence_Fast_GET_SIZE(fields.ptr());

            std::vector<std::string> out;
            out.reserve(static_cast<size_t>(size));
            for (Py_ssize_t i = 0; i < size; ++i) {
                out.push_back(this->stringify(items[i]));
            }
            return out;
        }

        std::vector<std::string> mapping_to_strings(nb::handle row) const {
            nb::object keys = this->sequence(this->fieldnames, "fieldnames must be a sequence");
            PyObject** items = PySequence_Fast_ITEMS(keys.ptr());
            const Py_ssize_t size = PySequence_Fast_GET_SIZE(keys.ptr());

            std::vector<std::string> out;
            out.reserve(static_cast<size_t>(size));
            for (Py_ssize_t i = 0; i < size; ++i) {
                PyObject* raw_value = PyObject_GetItem(row.ptr(), items[i]);
                if (raw_value == nullptr) {
                    if (PyErr_ExceptionMatches(PyExc_KeyError)) {
                        PyErr_Clear();
                        out.emplace_back();
                        continue;
                    }
                    throw nb::python_error();
                }

                nb::object value = nb::steal<nb::object>(raw_value);
                out.push_back(this->stringify(value.ptr()));
            }
            return out;
        }

        std::string stringify(PyObject* object) const {
            if (object == Py_None) {
                return {};
            }

            PyObject* raw_text = nullptr;
            if (PyUnicode_Check(object)) {
                raw_text = object;
                Py_INCREF(raw_text);
            }
            else {
                raw_text = PyObject_Str(object);
                if (raw_text == nullptr) {
                    throw nb::python_error();
                }
            }

            nb::object text = nb::steal<nb::object>(raw_text);
            Py_ssize_t size = 0;
            const char* data = PyUnicode_AsUTF8AndSize(text.ptr(), &size);
            if (data == nullptr) {
                throw nb::python_error();
            }
            return std::string(data, static_cast<size_t>(size));
        }
    };

    void write_csv_file(
        const std::string& filename,
        nb::handle rows,
        nb::object fieldnames,
        bool write_header,
        bool quote_minimal
    ) {
        std::ofstream out(filename, std::ios::out | std::ios::binary);
        if (!out.is_open()) {
            internals::throw_failed_open_for_writing(filename);
        }

        PythonRowWriter writer(out, std::move(fieldnames), write_header, quote_minimal);
        writer.write_all(rows);
    }

    void write_csv_filelike(
        nb::object output,
        nb::handle rows,
        nb::object fieldnames,
        bool write_header,
        bool quote_minimal
    ) {
        if (!PyObject_HasAttrString(output.ptr(), "write")) {
            throw nb::type_error("csvfile must be path-like or have a write() method");
        }

        PythonTextOutput out(std::move(output));
        PythonRowWriter<PythonTextOutput> writer(out, std::move(fieldnames), write_header, quote_minimal);
        writer.write_all(rows);
    }
}

void init_CSVWriter(nb::module_& m) {
    m.def(
        "_write_csv",
        &write_csv_file,
        "Write Python row iterables to a CSV file.",
        "filename"_a,
        "rows"_a,
        "fieldnames"_a = nb::none(),
        "write_header"_a = true,
        "quote_minimal"_a = true
    );
    m.def(
        "_write_csv_filelike",
        &write_csv_filelike,
        "Write Python row iterables to a text file-like object.",
        "csvfile"_a,
        "rows"_a,
        "fieldnames"_a = nb::none(),
        "write_header"_a = true,
        "quote_minimal"_a = true
    );
}
