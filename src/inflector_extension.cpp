#define DUCKDB_EXTENSION_MAIN

#include "inflector_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "rust.h"
#include "query_farm_telemetry.hpp"
namespace duckdb {

// Generic helper for string transformations
inline void RegisterInflectorTransform(ExtensionLoader &loader, const char *sql_name,
                                       char *(*cruet_func)(const char *)) {
	auto fun_impl = [cruet_func](DataChunk &args, ExpressionState &state, Vector &result) {
		auto &name_vector = args.data[0];
		UnaryExecutor::Execute<string_t, string_t>(
		    name_vector, result, args.size(), [cruet_func, &result](string_t name) -> string_t {
			    auto value = name.GetString();
			    char *change_result = cruet_func(value.c_str());
			    string_t return_result = StringVector::AddString(result, change_result);
			    free_c_string(change_result);
			    return return_result;
		    });
	};

	ScalarFunction fun(sql_name, {LogicalType::VARCHAR}, LogicalType::VARCHAR, fun_impl);
	loader.RegisterFunction(fun);
}

// Generic helper for boolean predicates
inline void RegisterInflectorPredicate(ExtensionLoader &loader, const char *sql_name,
                                       unsigned char (*cruet_func)(const char *)) {
	auto fun_impl = [cruet_func](DataChunk &args, ExpressionState &state, Vector &result) {
		auto &name_vector = args.data[0];
		UnaryExecutor::Execute<string_t, bool>(name_vector, result, args.size(), [cruet_func](string_t name) -> bool {
			auto value = name.GetString();
			return static_cast<bool>(cruet_func(value.c_str()));
		});
	};

	ScalarFunction fun(sql_name, {LogicalType::VARCHAR}, LogicalType::BOOLEAN, fun_impl);
	loader.RegisterFunction(fun);
}

struct InflectBindData : public FunctionData {
	InflectBindData() {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<InflectBindData>();
	}
	bool Equals(const FunctionData &other_p) const override {
		//		auto &other = other_p.Cast<CollectBindData>();
		return true;
	}
};

// Mapping from function name -> cruet transformer
using TransformFunc = char *(*)(const char *);

static unordered_map<string, TransformFunc> transformer_map = {
    {"camel", cruet_to_camel_case},       {"camel_case", cruet_to_camel_case},
    {"class", cruet_to_class_case},       {"class_case", cruet_to_class_case},
    {"snake", cruet_to_snake_case},       {"snake_case", cruet_to_snake_case},
    {"kebab", cruet_to_kebab_case},       {"kebab_case", cruet_to_kebab_case},
    {"train", cruet_to_train_case},       {"train_case", cruet_to_train_case},
    {"title", cruet_to_title_case},       {"title_case", cruet_to_title_case},
    {"table", cruet_to_table_case},       {"table_case", cruet_to_table_case},
    {"sentence", cruet_to_sentence_case}, {"sentence_case", cruet_to_sentence_case},
    {"upper", cruet_to_upper_case},       {"upper_case", cruet_to_upper_case},
    {"lower", cruet_to_lower_case},       {"lower_case", cruet_to_lower_case}};

static unique_ptr<FunctionData> InflectTableBind(ClientContext &context, TableFunctionBindInput &input,
                                                 vector<LogicalType> &return_types, vector<string> &names) {

	auto &function_name_value = input.inputs[0];
	if (function_name_value.IsNull()) {
		throw InvalidInputException("Function name cannot be NULL");
	}

	auto function_name = function_name_value.GetValue<string>();

	auto it = transformer_map.find(function_name);
	if (it == transformer_map.end()) {
		throw InvalidInputException("Unknown inflection '%s'. Supported: camel, class, snake, kebab, train, title, "
		                            "table, sentence, upper, lower",
		                            function_name.c_str());
	}
	TransformFunc transform = it->second;

	// Process each input column
	for (idx_t i = 0; i < input.input_table_types.size(); i++) {
		auto &part_name = input.input_table_names[i];
		auto &part_type = input.input_table_types[i];

		return_types.push_back(part_type);

		char *new_name = transform(part_name.c_str());
		names.emplace_back(new_name);
		free_c_string(new_name);
	}

	return make_uniq<InflectBindData>();
}

static OperatorResultType InflectInOut(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                       DataChunk &output) {
	output.Reset();
	output.Reference(input);
	output.Verify();
	return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType InflectInOutFinalize(ExecutionContext &context, TableFunctionInput &data_p,
                                                       DataChunk &output) {

	output.SetCardinality(0);
	output.Verify();
	return OperatorFinalizeResultType::FINISHED;
}

struct InflectScalarBindData : public FunctionData {
	InflectScalarBindData(char *(*transform_func_p)(const char *)) : transform_func(transform_func_p) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<InflectScalarBindData>(transform_func);
	}
	bool Equals(const FunctionData &other_p) const override {
		auto &other = other_p.Cast<InflectScalarBindData>();
		return transform_func == other.transform_func;
	}

	char *(*transform_func)(const char *);
};

LogicalType InflectLogicalType(const LogicalType &type, TransformFunc transform, bool recursive) {
	switch (type.id()) {

	case LogicalTypeId::STRUCT: {
		const auto &children = StructType::GetChildTypes(type);
		child_list_t<LogicalType> new_children;

		new_children.reserve(children.size());
		for (auto &child : children) {
			auto &name = child.first;
			auto &subtype = child.second;

			LogicalType updated_type = subtype;
			if (recursive) {
				updated_type = InflectLogicalType(subtype, transform, false);
			}

			// Apply name inflection here
			auto new_name = transform(name.c_str());
			new_children.emplace_back(new_name, updated_type);
			free_c_string(new_name);
		}

		return LogicalType::STRUCT(new_children);
	}

	case LogicalTypeId::LIST: {
		auto &child_type = ListType::GetChildType(type);

		// Recurse into element type if allowed
		LogicalType elem = child_type;
		if (recursive) {
			elem = InflectLogicalType(child_type, transform, true);
		}
		return LogicalType::LIST(elem);
	}

	case LogicalTypeId::MAP: {
		auto &key_type = MapType::KeyType(type);
		auto &value_type = MapType::ValueType(type);

		LogicalType new_key = key_type;
		LogicalType new_value = value_type;

		if (recursive) {
			new_key = InflectLogicalType(key_type, transform, true);
			new_value = InflectLogicalType(value_type, transform, true);
		}
		return LogicalType::MAP(new_key, new_value);
	}

	default:
		// Scalars and other types remain unchanged
		return type;
	}
}

unique_ptr<FunctionData> InflectScalarBind(ClientContext &context, ScalarFunction &bound_function,
                                           vector<unique_ptr<Expression>> &arguments) {

	if (arguments.size() != 2) {
		throw InvalidInputException("inflect() requires exactly two arguments: function name and value to inflect");
	}

	auto &arg = arguments[0];

	if (arg->HasParameter()) {
		throw ParameterNotResolvedException();
	}
	if (!arg->IsFoldable()) {
		throw BinderException("inflect: format argument must be constant");
	}

	if (arg->return_type.id() != LogicalTypeId::VARCHAR) {
		throw InvalidInputException("First argument to inflect() must be a VARCHAR function name");
	}

	auto function_name = StringValue::Get(ExpressionExecutor::EvaluateScalar(context, *arg));

	// The format name need to be constant.
	auto it = transformer_map.find(function_name);
	if (it == transformer_map.end()) {
		throw InvalidInputException("Unknown inflection '%s'. Supported: camel, class, snake, kebab, train, title, "
		                            "table, sentence, upper, lower",
		                            function_name.c_str());
	}
	TransformFunc transform = it->second;

	// We should deal with the type here now.
	bound_function.return_type = InflectLogicalType(arguments[1]->return_type, transform, true);

	return make_uniq<InflectScalarBindData>(transform);
}

void InflectStringFunc(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &type_vector = args.data[0];
	auto &source = args.data[1];

	BinaryExecutor::Execute<string_t, string_t, string_t>(
	    type_vector, source, result, args.size(), [&result](string_t name, string_t data) -> string_t {
		    auto function_name = name.GetString();
		    auto value = data.GetString();

		    auto it = transformer_map.find(function_name);
		    if (it == transformer_map.end()) {
			    throw InvalidInputException(
			        "Unknown inflection '%s'. Supported: camel, class, snake, kebab, train, title, "
			        "table, sentence, upper, lower",
			        function_name.c_str());
		    }
		    TransformFunc transform = it->second;

		    char *change_result = transform(value.c_str());
		    string_t return_result = StringVector::AddString(result, change_result);
		    free_c_string(change_result);
		    return return_result;
	    });
}

void InflectScalarFunc(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &source = args.data[1];

	if (!(result.GetType().IsNested() && result.GetType().InternalType() == PhysicalType::STRUCT)) {
		result.Reference(source);
		result.Verify(args.size());
		return;
	}

	auto &source_vectors = StructVector::GetEntries(source);
	auto &target_children = StructVector::GetEntries(result);

	for (idx_t i = 0; i < source_vectors.size(); i++) {
		auto &source_vector = *source_vectors[i];
		auto &target_vector = *target_children[i];

		target_vector.Reference(source_vector);
	}

	if (source.GetVectorType() == VectorType::CONSTANT_VECTOR) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	} else {
		auto &result_validity = FlatVector::Validity(result);
		result_validity = FlatVector::Validity(source);
	}
	result.Verify(args.size());
}

// Load all inflector functions
void LoadInternal(ExtensionLoader &loader) {
	// Transform functions
	RegisterInflectorTransform(loader, "inflector_to_class_case", cruet_to_class_case);
	RegisterInflectorTransform(loader, "inflector_to_camel_case", cruet_to_camel_case);
	RegisterInflectorTransform(loader, "inflector_to_pascal_case", cruet_to_pascal_case);
	RegisterInflectorTransform(loader, "inflector_to_screamingsnake_case", cruet_to_screamingsnake_case);
	RegisterInflectorTransform(loader, "inflector_to_snake_case", cruet_to_snake_case);
	RegisterInflectorTransform(loader, "inflector_to_kebab_case", cruet_to_kebab_case);
	RegisterInflectorTransform(loader, "inflector_to_train_case", cruet_to_train_case);
	RegisterInflectorTransform(loader, "inflector_to_sentence_case", cruet_to_sentence_case);
	RegisterInflectorTransform(loader, "inflector_to_title_case", cruet_to_title_case);
	RegisterInflectorTransform(loader, "inflector_to_table_case", cruet_to_table_case);
	RegisterInflectorTransform(loader, "inflector_ordinalize", cruet_ordinalize);
	RegisterInflectorTransform(loader, "inflector_deordinalize", cruet_deordinalize);
	RegisterInflectorTransform(loader, "inflector_to_foreign_key", cruet_to_foreign_key);
	RegisterInflectorTransform(loader, "inflector_demodulize", cruet_demodulize);
	RegisterInflectorTransform(loader, "inflector_deconstantize", cruet_deconstantize);
	RegisterInflectorTransform(loader, "inflector_to_plural", cruet_to_plural);
	RegisterInflectorTransform(loader, "inflector_to_singular", cruet_to_singular);

	// Predicate functions
	RegisterInflectorPredicate(loader, "inflector_is_class_case", cruet_is_class_case);
	RegisterInflectorPredicate(loader, "inflector_is_camel_case", cruet_is_camel_case);
	RegisterInflectorPredicate(loader, "inflector_is_pascal_case", cruet_is_pascal_case);
	RegisterInflectorPredicate(loader, "inflector_is_screamingsnake_case", cruet_is_screamingsnake_case);
	RegisterInflectorPredicate(loader, "inflector_is_snake_case", cruet_is_snake_case);
	RegisterInflectorPredicate(loader, "inflector_is_kebab_case", cruet_is_kebab_case);
	RegisterInflectorPredicate(loader, "inflector_is_train_case", cruet_is_train_case);
	RegisterInflectorPredicate(loader, "inflector_is_sentence_case", cruet_is_sentence_case);
	RegisterInflectorPredicate(loader, "inflector_is_title_case", cruet_is_title_case);
	RegisterInflectorPredicate(loader, "inflector_is_table_case", cruet_is_table_case);
	RegisterInflectorPredicate(loader, "inflector_is_foreign_key", cruet_is_foreign_key);

	auto inflect_table_function =
	    TableFunction("inflect", {LogicalType::VARCHAR, LogicalType::TABLE}, nullptr, InflectTableBind);
	inflect_table_function.in_out_function = InflectInOut;
	inflect_table_function.in_out_function_final = InflectInOutFinalize;
	loader.RegisterFunction(inflect_table_function);

	// Now need a scalar function that will inflect a struct type.
	auto scalar_function_set = ScalarFunctionSet("inflect");
	auto inflect_string_function = ScalarFunction("inflect", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                              LogicalType::VARCHAR, InflectStringFunc);
	scalar_function_set.AddFunction(inflect_string_function);

	auto inflect_struct_function = ScalarFunction("inflect", {LogicalType::VARCHAR, LogicalType::ANY}, LogicalType::ANY,
	                                              InflectScalarFunc, InflectScalarBind);
	scalar_function_set.AddFunction(inflect_struct_function);

	loader.RegisterFunction(scalar_function_set);

	QueryFarmSendTelemetry(loader, "inflector", "2025110901");
}

void InflectorExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string InflectorExtension::Name() {
	return "inflector";
}

std::string InflectorExtension::Version() const {
	return "2025110801";
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(inflector, loader) {
	duckdb::LoadInternal(loader);
}
}
