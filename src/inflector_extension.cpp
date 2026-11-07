#define DUCKDB_EXTENSION_MAIN

#include "inflector_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/execution/expression_executor_state.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/config.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <duckdb/parser/parsed_data/create_table_function_info.hpp>
#include <duckdb/execution/expression_executor.hpp>
#include <duckdb/planner/expression/bound_function_expression.hpp>
#include "duckdb/common/vector/constant_vector.hpp"
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/string_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#include "rust.h"
#include "query_farm_telemetry.hpp"
#include "utf8proc.hpp"

namespace duckdb {

//! Single source of truth for the extension version (telemetry + Version()).
static constexpr const char *INFLECTOR_EXTENSION_VERSION = "2026110701";

//! A positional-only parameter with a real name (bare types would be reported as col0, col1, ...)
static FunctionParameter Positional(const char *name, LogicalType type) {
	return FunctionParameter(name, std::move(type), {}, FunctionParameterKind::POSITIONAL_ONLY);
}

static std::string StripAccentsString(const std::string &input) {
	auto stripped = utf8proc_remove_accents((const utf8proc_uint8_t *)input.c_str(), (utf8proc_ssize_t)input.size());
	if (!stripped) {
		throw InvalidInputException("inflector: could not strip accents (input is not valid UTF-8)");
	}
	std::string result((const char *)stripped);
	free(stripped);
	return result;
}

//===--------------------------------------------------------------------===//
// Rust FFI helpers
//===--------------------------------------------------------------------===//

static void CheckInflectorStatus(InflectorStatus status) {
	switch (status) {
	case InflectorStatus_Ok:
		return;
	case InflectorStatus_InvalidUtf8:
		throw InvalidInputException("inflector: input is not valid UTF-8");
	case InflectorStatus_NullArgument:
		throw InvalidInputException("inflector: internal error (NULL argument passed to the Rust binding)");
	default:
		throw InvalidInputException("inflector: internal error while transforming the input (caught a Rust panic)");
	}
}

//! Owns a buffer returned by inflector_transform
struct InflectorBuffer {
	uint8_t *data = nullptr;
	uintptr_t len = 0;
	~InflectorBuffer() {
		inflector_free_buffer(data, len);
	}
};

static string_t InflectToVector(InflectorOp op, const string_t &input, const InflectorAcronyms *acronyms,
                                Vector &result) {
	InflectorBuffer buffer;
	CheckInflectorStatus(inflector_transform(op, reinterpret_cast<const uint8_t *>(input.GetData()), input.GetSize(),
	                                         acronyms, &buffer.data, &buffer.len));
	return StringVector::AddString(result, reinterpret_cast<const char *>(buffer.data), buffer.len);
}

static std::string InflectToString(InflectorOp op, const std::string &input, const InflectorAcronyms *acronyms) {
	InflectorBuffer buffer;
	CheckInflectorStatus(inflector_transform(op, reinterpret_cast<const uint8_t *>(input.data()), input.size(),
	                                         acronyms, &buffer.data, &buffer.len));
	return std::string(reinterpret_cast<const char *>(buffer.data), buffer.len);
}

static bool InflectPredicate(InflectorOp op, const string_t &input, const InflectorAcronyms *acronyms) {
	bool out = false;
	CheckInflectorStatus(inflector_predicate(op, reinterpret_cast<const uint8_t *>(input.GetData()), input.GetSize(),
	                                         acronyms, &out));
	return out;
}

//===--------------------------------------------------------------------===//
// inflector_acronyms setting (session scoped: read from the ClientContext)
//===--------------------------------------------------------------------===//

static constexpr const char *ACRONYMS_SETTING = "inflector_acronyms";

using AcronymsHandle = std::shared_ptr<InflectorAcronyms>;

//! Read the current (session/global) value of inflector_acronyms. Returns nullptr when no acronyms are set.
static AcronymsHandle LoadAcronyms(ClientContext &context) {
	Value value;
	if (!context.TryGetCurrentSetting(ACRONYMS_SETTING, value) || value.IsNull()) {
		return nullptr;
	}
	string csv;
	for (auto &child : ListValue::GetChildren(value)) {
		if (child.IsNull()) {
			continue;
		}
		if (!csv.empty()) {
			csv += ",";
		}
		csv += child.ToString();
	}
	if (csv.empty()) {
		return nullptr;
	}
	auto raw = inflector_acronyms_new(reinterpret_cast<const uint8_t *>(csv.data()), csv.size());
	if (!raw) {
		throw InvalidInputException("inflector: invalid value for setting inflector_acronyms");
	}
	return AcronymsHandle(raw, inflector_acronyms_free);
}

//! Thread-local execution state: the acronyms in effect for the executing connection
struct InflectorLocalState : public FunctionLocalState {
	AcronymsHandle acronyms;
};

static unique_ptr<FunctionLocalState> InflectorInitLocalState(ExpressionState &state,
                                                              const BoundFunctionExpression &expr,
                                                              FunctionData *bind_data) {
	auto result = make_uniq<InflectorLocalState>();
	if (state.HasContext()) {
		result->acronyms = LoadAcronyms(state.GetContext());
	}
	return std::move(result);
}

static const InflectorAcronyms *GetLocalAcronyms(ExpressionState &state) {
	auto local_state = ExecuteFunctionState::GetFunctionState(state);
	if (!local_state) {
		return nullptr;
	}
	return local_state->Cast<InflectorLocalState>().acronyms.get();
}

//===--------------------------------------------------------------------===//
// Name renaming helpers (struct fields and table columns)
//===--------------------------------------------------------------------===//

//! Inflect an identifier. An empty result (e.g. a name made only of separators) keeps the original name.
static std::string InflectName(InflectorOp op, const std::string &name, bool strip_accents,
                               const InflectorAcronyms *acronyms) {
	auto input = strip_accents ? StripAccentsString(name) : name;
	auto result = InflectToString(op, input, acronyms);
	if (result.empty()) {
		return name;
	}
	return result;
}

//! Make names unique (case-insensitively, as DuckDB identifiers are) by appending _1, _2, ... to later
//! duplicates, in input order. E.g. [fooBar, foo_bar] inflected to snake case gives [foo_bar, foo_bar_1].
static void DeduplicateNames(vector<std::string> &names) {
	case_insensitive_set_t seen;
	for (auto &name : names) {
		std::string candidate = name;
		idx_t suffix = 1;
		while (seen.count(candidate)) {
			candidate = name + "_" + std::to_string(suffix++);
		}
		seen.insert(candidate);
		name = std::move(candidate);
	}
}

// Generic helper for string transformations with documentation
static void RegisterInflectorTransform(ExtensionLoader &loader, const char *sql_name, InflectorOp op,
                                       const char *description, const char *example, const char *param_name,
                                       vector<string> categories, bool strip_accents_first = false) {
	auto fun_impl = [op, strip_accents_first](DataChunk &args, ExpressionState &state, Vector &result) {
		auto acronyms = GetLocalAcronyms(state);
		UnaryExecutor::Execute<string_t, string_t>(
		    args.data[0], result, [&](string_t input) -> string_t {
			    if (strip_accents_first) {
				    auto stripped = StripAccentsString(input.GetString());
				    return InflectToVector(op, string_t(stripped.data(), stripped.size()), acronyms, result);
			    }
			    return InflectToVector(op, input, acronyms, result);
		    });
	};

	ScalarFunction fun(sql_name, {Positional(param_name, LogicalType::VARCHAR)}, LogicalType::VARCHAR, fun_impl);
	fun.SetInitStateCallback(InflectorInitLocalState);
	fun.SetFallible();
	CreateScalarFunctionInfo info(fun);

	FunctionDescription func_desc;
	func_desc.description = description;
	func_desc.examples.push_back(example);
	func_desc.parameter_names.push_back(param_name);
	func_desc.parameter_types.push_back(LogicalType::VARCHAR);
	func_desc.categories = std::move(categories);
	info.descriptions.push_back(std::move(func_desc));

	loader.RegisterFunction(info);
}

// Generic helper for boolean predicates with documentation
static void RegisterInflectorPredicate(ExtensionLoader &loader, const char *sql_name, InflectorOp op,
                                       const char *description, const char *example, const char *param_name,
                                       vector<string> categories) {
	auto fun_impl = [op](DataChunk &args, ExpressionState &state, Vector &result) {
		auto acronyms = GetLocalAcronyms(state);
		UnaryExecutor::Execute<string_t, bool>(args.data[0], result, [&](string_t input) -> bool {
			return InflectPredicate(op, input, acronyms);
		});
	};

	ScalarFunction fun(sql_name, {Positional(param_name, LogicalType::VARCHAR)}, LogicalType::BOOLEAN, fun_impl);
	fun.SetInitStateCallback(InflectorInitLocalState);
	fun.SetFallible();
	CreateScalarFunctionInfo info(fun);

	FunctionDescription func_desc;
	func_desc.description = description;
	func_desc.examples.push_back(example);
	func_desc.parameter_names.push_back(param_name);
	func_desc.parameter_types.push_back(LogicalType::VARCHAR);
	func_desc.categories = std::move(categories);
	info.descriptions.push_back(std::move(func_desc));

	loader.RegisterFunction(info);
}

// Mapping from format name -> operation, for inflect()
static const unordered_map<string, InflectorOp> transformer_map = {
    {"camel", InflectorOp_CamelCase},       {"camel_case", InflectorOp_CamelCase},
    {"class", InflectorOp_ClassCase},       {"class_case", InflectorOp_ClassCase},
    {"pascal", InflectorOp_PascalCase},     {"pascal_case", InflectorOp_PascalCase},
    {"snake", InflectorOp_SnakeCase},       {"snake_case", InflectorOp_SnakeCase},
    {"kebab", InflectorOp_KebabCase},       {"kebab_case", InflectorOp_KebabCase},
    {"train", InflectorOp_TrainCase},       {"train_case", InflectorOp_TrainCase},
    {"title", InflectorOp_TitleCase},       {"title_case", InflectorOp_TitleCase},
    {"table", InflectorOp_TableCase},       {"table_case", InflectorOp_TableCase},
    {"sentence", InflectorOp_SentenceCase}, {"sentence_case", InflectorOp_SentenceCase},
    {"upper", InflectorOp_UpperCase},       {"upper_case", InflectorOp_UpperCase},
    {"lower", InflectorOp_LowerCase},       {"lower_case", InflectorOp_LowerCase}};

static InflectorOp LookupFormat(const string &format) {
	auto it = transformer_map.find(format);
	if (it == transformer_map.end()) {
		throw InvalidInputException("Unknown inflection '%s'. Supported: camel, class, pascal, snake, kebab, train, title, "
		                            "table, sentence, upper, lower",
		                            format.c_str());
	}
	return it->second;
}

//===--------------------------------------------------------------------===//
// inflect(format, TABLE) table function
//===--------------------------------------------------------------------===//

static unique_ptr<FunctionData> InflectTableBind(ClientContext &context, TableFunctionBindInput &input,
                                                 vector<LogicalType> &return_types, vector<Identifier> &names) {
	auto &function_name_value = input.inputs[0];
	if (function_name_value.IsNull()) {
		throw InvalidInputException("inflect: format cannot be NULL");
	}
	auto op = LookupFormat(function_name_value.GetValue<string>());

	bool strip_accents = false;
	auto sa_it = input.named_parameters.find("strip_accents");
	if (sa_it != input.named_parameters.end()) {
		if (sa_it->second.IsNull()) {
			throw InvalidInputException("inflect: strip_accents cannot be NULL");
		}
		strip_accents = sa_it->second.GetValue<bool>();
	}

	auto acronyms = LoadAcronyms(context);
	vector<std::string> new_names;
	for (idx_t i = 0; i < input.input_table_types.size(); i++) {
		return_types.push_back(input.input_table_types[i]);
		new_names.push_back(
		    InflectName(op, input.input_table_names[i].GetIdentifierName(), strip_accents, acronyms.get()));
	}
	DeduplicateNames(new_names);
	for (auto &name : new_names) {
		names.emplace_back(name);
	}
	return make_uniq<TableFunctionData>();
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

//===--------------------------------------------------------------------===//
// inflect(format, value [, strip_accents]) scalar functions
//===--------------------------------------------------------------------===//

struct InflectScalarBindData : public FunctionData {
	InflectScalarBindData(InflectorOp op_p, bool strip_accents_p) : op(op_p), strip_accents(strip_accents_p) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<InflectScalarBindData>(op, strip_accents);
	}
	bool Equals(const FunctionData &other_p) const override {
		auto &other = other_p.Cast<InflectScalarBindData>();
		return op == other.op && strip_accents == other.strip_accents;
	}

	InflectorOp op;
	bool strip_accents;
};

static LogicalType InflectLogicalType(const LogicalType &type, InflectorOp op, bool strip_accents,
                                      const InflectorAcronyms *acronyms) {
	switch (type.id()) {
	case LogicalTypeId::STRUCT: {
		const auto &children = StructType::GetChildTypes(type);
		vector<std::string> new_names;
		vector<LogicalType> new_types;
		for (auto &child : children) {
			new_types.push_back(InflectLogicalType(child.second, op, strip_accents, acronyms));
			new_names.push_back(InflectName(op, child.first.GetIdentifierName(), strip_accents, acronyms));
		}
		DeduplicateNames(new_names);
		child_list_t<LogicalType> new_children;
		new_children.reserve(children.size());
		for (idx_t i = 0; i < new_names.size(); i++) {
			new_children.emplace_back(new_names[i], new_types[i]);
		}
		return LogicalType::STRUCT(new_children);
	}
	case LogicalTypeId::LIST:
		return LogicalType::LIST(InflectLogicalType(ListType::GetChildType(type), op, strip_accents, acronyms));
	case LogicalTypeId::MAP:
		return LogicalType::MAP(InflectLogicalType(MapType::KeyType(type), op, strip_accents, acronyms),
		                        InflectLogicalType(MapType::ValueType(type), op, strip_accents, acronyms));
	default:
		// Scalars and other types remain unchanged
		return type;
	}
}

static Value EvaluateConstantArgument(ClientContext &context, Expression &arg, const char *what) {
	if (arg.HasParameter()) {
		throw ParameterNotResolvedException();
	}
	if (!arg.IsFoldable()) {
		throw BinderException("inflect: %s argument must be constant", what);
	}
	auto value = ExpressionExecutor::EvaluateScalar(context, arg);
	if (value.IsNull()) {
		throw InvalidInputException("inflect: %s cannot be NULL", what);
	}
	return value;
}

static unique_ptr<FunctionData> InflectScalarBind(BindScalarFunctionInput &input) {
	auto &context = input.GetClientContext();
	auto &bound_function = input.GetBoundFunction();
	auto &arguments = input.GetArguments();

	if (arguments.size() < 2 || arguments.size() > 3) {
		throw InvalidInputException("inflect() requires 2 or 3 arguments: format, value, and optionally strip_accents");
	}
	if (arguments[0]->GetReturnType().id() != LogicalTypeId::VARCHAR) {
		throw InvalidInputException("First argument to inflect() must be a VARCHAR function name");
	}

	auto op = LookupFormat(StringValue::Get(EvaluateConstantArgument(context, *arguments[0], "format")));

	bool strip_accents = false;
	if (arguments.size() == 3) {
		strip_accents =
		    BooleanValue::Get(EvaluateConstantArgument(context, *arguments[2], "strip_accents")
		                          .DefaultCastAs(LogicalType::BOOLEAN));
	}

	// Struct field names are part of the return type, so they are renamed with the acronyms in effect at bind time.
	if (arguments[1]->GetReturnType().id() != LogicalTypeId::VARCHAR) {
		auto acronyms = LoadAcronyms(context);
		bound_function.SetReturnType(
		    InflectLogicalType(arguments[1]->GetReturnType(), op, strip_accents, acronyms.get()));
	}

	return make_uniq<InflectScalarBindData>(op, strip_accents);
}

static void InflectStringFunc(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &func_expr = state.expr.Cast<BoundFunctionExpression>();
	auto &bind_data = func_expr.BindInfo()->Cast<InflectScalarBindData>();
	auto op = bind_data.op;
	auto strip_accents = bind_data.strip_accents;
	auto acronyms = GetLocalAcronyms(state);

	// The format is a bind-time constant (validated in InflectScalarBind), so only the value is read per row.
	UnaryExecutor::Execute<string_t, string_t>(args.data[1], result, [&](string_t input) -> string_t {
		if (strip_accents) {
			auto stripped = StripAccentsString(input.GetString());
			return InflectToVector(op, string_t(stripped.data(), stripped.size()), acronyms, result);
		}
		return InflectToVector(op, input, acronyms, result);
	});
}

// Recursively populate `result` from `source` where the two vectors have
// physically identical layouts but may differ in struct field names. The
// result vector's type tree (already set by InflectScalarBind to the
// renamed type) is preserved; data is copied positionally so renames at
// every nesting level take effect.
static void InflectRenameCopy(Vector &source, Vector &result, idx_t count) {
	switch (result.GetType().id()) {
	case LogicalTypeId::STRUCT: {
		source.Flatten();
		auto &source_children = StructVector::GetEntries(source);
		auto &result_children = StructVector::GetEntries(result);
		D_ASSERT(source_children.size() == result_children.size());

		for (idx_t i = 0; i < result_children.size(); i++) {
			InflectRenameCopy(source_children[i], result_children[i], count);
		}
		FlatVector::CopyValidity(result, source, count);
		FlatVector::SetSize(result, count);
		return;
	}
	case LogicalTypeId::LIST:
	case LogicalTypeId::MAP: {
		source.Flatten();

		auto src_entries = FlatVector::GetData<list_entry_t>(source);
		auto res_entries = FlatVector::GetDataMutable<list_entry_t>(result);
		memcpy(res_entries, src_entries, sizeof(list_entry_t) * count);
		FlatVector::CopyValidity(result, source, count);

		idx_t list_size = ListVector::GetListSize(source);
		ListVector::Reserve(result, list_size);

		auto &source_child = ListVector::GetChildMutable(source);
		auto &result_child = ListVector::GetChildMutable(result);
		InflectRenameCopy(source_child, result_child, list_size);
		ListVector::SetListSize(result, list_size);
		FlatVector::SetSize(result, count);
		return;
	}
	default:
		// Leaf or non-renamed-aware type: types are identical, plain reference is safe.
		result.Reference(source);
		return;
	}
}

static void InflectScalarFunc(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &source = args.data[1];
	InflectRenameCopy(source, result, args.size());
	result.Verify();
}

// Load all inflector functions
void LoadInternal(ExtensionLoader &loader) {
	// Transform functions - case conversion
	RegisterInflectorTransform(loader, "inflector_to_class_case", InflectorOp_ClassCase,
	                           "Converts a string to ClassCase (PascalCase) format",
	                           "inflector_to_class_case('hello_world')", "text", {"text", "case_conversion"});
	RegisterInflectorTransform(loader, "inflector_to_camel_case", InflectorOp_CamelCase,
	                           "Converts a string to camelCase format",
	                           "inflector_to_camel_case('hello_world')", "text", {"text", "case_conversion"});
	RegisterInflectorTransform(loader, "inflector_to_pascal_case", InflectorOp_PascalCase,
	                           "Converts a string to PascalCase format",
	                           "inflector_to_pascal_case('hello_world')", "text", {"text", "case_conversion"});
	RegisterInflectorTransform(loader, "inflector_to_screamingsnake_case", InflectorOp_ScreamingSnakeCase,
	                           "Converts a string to SCREAMING_SNAKE_CASE format",
	                           "inflector_to_screamingsnake_case('helloWorld')", "text", {"text", "case_conversion"});
	RegisterInflectorTransform(loader, "inflector_to_snake_case", InflectorOp_SnakeCase,
	                           "Converts a string to snake_case format",
	                           "inflector_to_snake_case('helloWorld')", "text", {"text", "case_conversion"});
	RegisterInflectorTransform(loader, "inflector_to_kebab_case", InflectorOp_KebabCase,
	                           "Converts a string to kebab-case format",
	                           "inflector_to_kebab_case('helloWorld')", "text", {"text", "case_conversion"});
	RegisterInflectorTransform(loader, "inflector_to_train_case", InflectorOp_TrainCase,
	                           "Converts a string to Train-Case format",
	                           "inflector_to_train_case('helloWorld')", "text", {"text", "case_conversion"});
	RegisterInflectorTransform(loader, "inflector_to_sentence_case", InflectorOp_SentenceCase,
	                           "Converts a string to Sentence case format",
	                           "inflector_to_sentence_case('helloWorld')", "text", {"text", "case_conversion"});
	RegisterInflectorTransform(loader, "inflector_to_title_case", InflectorOp_TitleCase,
	                           "Converts a string to Title Case format",
	                           "inflector_to_title_case('hello_world')", "text", {"text", "case_conversion"});

	// Transform functions - naming conventions
	RegisterInflectorTransform(loader, "inflector_to_table_case", InflectorOp_TableCase,
	                           "Converts a string to table_cases format (snake_case plural)",
	                           "inflector_to_table_case('FooBar')", "text", {"text", "naming"});
	RegisterInflectorTransform(loader, "inflector_to_foreign_key", InflectorOp_ForeignKey,
	                           "Converts a class name to a foreign key column name",
	                           "inflector_to_foreign_key('Message')", "class_name", {"text", "naming"});
	RegisterInflectorTransform(loader, "inflector_demodulize", InflectorOp_Demodulize,
	                           "Removes the module part from a fully qualified name",
	                           "inflector_demodulize('ActiveRecord::CoreExtensions::String')", "qualified_name", {"text", "naming"});
	RegisterInflectorTransform(loader, "inflector_deconstantize", InflectorOp_Deconstantize,
	                           "Removes the rightmost segment from a constant expression",
	                           "inflector_deconstantize('Net::HTTP')", "constant", {"text", "naming"});

	// Transform functions - inflection
	RegisterInflectorTransform(loader, "inflector_ordinalize", InflectorOp_Ordinalize,
	                           "Converts a number string to its ordinal form (1st, 2nd, 3rd, etc.)",
	                           "inflector_ordinalize('1')", "number", {"text", "inflection"});
	RegisterInflectorTransform(loader, "inflector_deordinalize", InflectorOp_Deordinalize,
	                           "Removes the ordinal suffix from a string (1st -> 1)",
	                           "inflector_deordinalize('1st')", "ordinal", {"text", "inflection"});
	RegisterInflectorTransform(loader, "inflector_to_plural", InflectorOp_Plural,
	                           "Returns the plural form of a word",
	                           "inflector_to_plural('person')", "word", {"text", "inflection"});
	RegisterInflectorTransform(loader, "inflector_to_singular", InflectorOp_Singular,
	                           "Returns the singular form of a word",
	                           "inflector_to_singular('people')", "word", {"text", "inflection"});

	RegisterInflectorTransform(loader, "inflector_humanize", InflectorOp_Humanize,
	                           "Turns an identifier into a human-readable phrase: drops a trailing _id and renders "
	                           "Sentence case (Rails humanize)",
	                           "inflector_humanize('author_id')", "text", {"text", "inflection"});
	RegisterInflectorTransform(loader, "inflector_parameterize", InflectorOp_Parameterize,
	                           "Converts a string to a URL slug: accents removed, lowercased, and every run of "
	                           "non-alphanumeric characters replaced by a single '-'",
	                           "inflector_parameterize('Hello, World: A Recipe!')", "text", {"text", "naming"},
	                           true);
	RegisterInflectorTransform(loader, "inflector_classify", InflectorOp_Classify,
	                           "Converts a table name to a class name: singularized PascalCase (Rails classify)",
	                           "inflector_classify('egg_and_hams')", "table_name", {"text", "naming"});

	// Predicate functions - case detection
	RegisterInflectorPredicate(loader, "inflector_is_class_case", InflectorOp_ClassCase,
	                           "Returns true if the string is in ClassCase (PascalCase) format",
	                           "inflector_is_class_case('HelloWorld')", "text", {"text", "case_detection"});
	RegisterInflectorPredicate(loader, "inflector_is_camel_case", InflectorOp_CamelCase,
	                           "Returns true if the string is in camelCase format",
	                           "inflector_is_camel_case('helloWorld')", "text", {"text", "case_detection"});
	RegisterInflectorPredicate(loader, "inflector_is_pascal_case", InflectorOp_PascalCase,
	                           "Returns true if the string is in PascalCase format",
	                           "inflector_is_pascal_case('HelloWorld')", "text", {"text", "case_detection"});
	RegisterInflectorPredicate(loader, "inflector_is_screamingsnake_case", InflectorOp_ScreamingSnakeCase,
	                           "Returns true if the string is in SCREAMING_SNAKE_CASE format",
	                           "inflector_is_screamingsnake_case('HELLO_WORLD')", "text", {"text", "case_detection"});
	RegisterInflectorPredicate(loader, "inflector_is_snake_case", InflectorOp_SnakeCase,
	                           "Returns true if the string is in snake_case format",
	                           "inflector_is_snake_case('hello_world')", "text", {"text", "case_detection"});
	RegisterInflectorPredicate(loader, "inflector_is_kebab_case", InflectorOp_KebabCase,
	                           "Returns true if the string is in kebab-case format",
	                           "inflector_is_kebab_case('hello-world')", "text", {"text", "case_detection"});
	RegisterInflectorPredicate(loader, "inflector_is_train_case", InflectorOp_TrainCase,
	                           "Returns true if the string is in Train-Case format",
	                           "inflector_is_train_case('Hello-World')", "text", {"text", "case_detection"});
	RegisterInflectorPredicate(loader, "inflector_is_sentence_case", InflectorOp_SentenceCase,
	                           "Returns true if the string is in Sentence case format",
	                           "inflector_is_sentence_case('Hello world')", "text", {"text", "case_detection"});
	RegisterInflectorPredicate(loader, "inflector_is_title_case", InflectorOp_TitleCase,
	                           "Returns true if the string is in Title Case format",
	                           "inflector_is_title_case('Hello World')", "text", {"text", "case_detection"});

	// Predicate functions - naming detection
	RegisterInflectorPredicate(loader, "inflector_is_table_case", InflectorOp_TableCase,
	                           "Returns true if the string is in table_case format (snake_case plural)",
	                           "inflector_is_table_case('foo_bars')", "text", {"text", "naming"});
	RegisterInflectorPredicate(loader, "inflector_is_foreign_key", InflectorOp_ForeignKey,
	                           "Returns true if the string is in foreign key format (ends with _id)",
	                           "inflector_is_foreign_key('message_id')", "text", {"text", "naming"});

	// Table function: inflect column names in query results
	auto inflect_table_function =
	    TableFunction("inflect",
	                  FunctionSignature({Positional("format", LogicalType::VARCHAR),
	                                     Positional("query", LogicalType::TABLE)},
	                                    LogicalType::INVALID),
	                  nullptr, InflectTableBind);
	inflect_table_function.in_out_function = InflectInOut;
	inflect_table_function.in_out_function_final = InflectInOutFinalize;
	inflect_table_function.GetSignature().WithTypedKwargs(
	    "options", [](TypedKwargs &options) { options.Add("strip_accents", LogicalType::BOOLEAN); });
	CreateTableFunctionInfo table_func_info(inflect_table_function);
	FunctionDescription table_func_desc;
	table_func_desc.description = "Transforms column names in query results using the specified case format. Names "
	                               "that collide after renaming get a _1, _2, ... suffix (in column order)";
	table_func_desc.examples.push_back("FROM inflect('snake', (SELECT 'John' AS firstName, 'Doe' AS lastName))");
	table_func_desc.parameter_names.push_back("format");
	table_func_desc.parameter_names.push_back("query");
	table_func_desc.parameter_types.push_back(LogicalType::VARCHAR);
	table_func_desc.parameter_types.push_back(LogicalType::TABLE);
	table_func_desc.categories.push_back("text");
	table_func_desc.categories.push_back("case_conversion");
	table_func_info.descriptions.push_back(std::move(table_func_desc));
	loader.RegisterFunction(table_func_info);

	// Scalar functions: inflect string values or struct field names
	auto scalar_function_set = ScalarFunctionSet("inflect");
	auto inflect_string_function =
	    ScalarFunction("inflect", {Positional("format", LogicalType::VARCHAR), Positional("text", LogicalType::VARCHAR)},
	                   LogicalType::VARCHAR, InflectStringFunc, InflectScalarBind);
	inflect_string_function.SetInitStateCallback(InflectorInitLocalState);
	scalar_function_set.AddFunction(inflect_string_function);

	auto inflect_struct_function =
	    ScalarFunction("inflect", {Positional("format", LogicalType::VARCHAR), Positional("value", LogicalType::ANY)},
	                   LogicalType::ANY, InflectScalarFunc, InflectScalarBind);
	scalar_function_set.AddFunction(inflect_struct_function);

	// 3-argument overloads with strip_accents boolean
	auto inflect_string_sa_function =
	    ScalarFunction("inflect",
	                   {Positional("format", LogicalType::VARCHAR), Positional("text", LogicalType::VARCHAR),
	                    Positional("strip_accents", LogicalType::BOOLEAN)},
	                   LogicalType::VARCHAR, InflectStringFunc, InflectScalarBind);
	inflect_string_sa_function.SetInitStateCallback(InflectorInitLocalState);
	scalar_function_set.AddFunction(inflect_string_sa_function);

	auto inflect_struct_sa_function =
	    ScalarFunction("inflect",
	                   {Positional("format", LogicalType::VARCHAR), Positional("value", LogicalType::ANY),
	                    Positional("strip_accents", LogicalType::BOOLEAN)},
	                   LogicalType::ANY, InflectScalarFunc, InflectScalarBind);
	scalar_function_set.AddFunction(inflect_struct_sa_function);

	scalar_function_set.SetFallible();
	CreateScalarFunctionInfo scalar_func_info(scalar_function_set);

	auto add_description = [&](const char *description, const char *example, vector<string> names,
	                           vector<LogicalType> types, vector<string> categories) {
		FunctionDescription desc;
		desc.description = description;
		desc.examples.push_back(example);
		desc.parameter_names = std::move(names);
		desc.parameter_types = std::move(types);
		desc.categories = std::move(categories);
		scalar_func_info.descriptions.push_back(std::move(desc));
	};
	add_description("Transforms a string value using the specified case format", "inflect('snake', 'helloWorld')",
	                {"format", "text"}, {LogicalType::VARCHAR, LogicalType::VARCHAR}, {"text", "case_conversion"});
	add_description("Transforms struct field names using the specified case format (recursively, through nested "
	                "structs, lists and maps). Field names that collide after renaming get a _1, _2, ... suffix",
	                "inflect('snake', {firstName: 'John', lastName: 'Doe'})", {"format", "value"},
	                {LogicalType::VARCHAR, LogicalType::ANY}, {"struct", "case_conversion"});
	add_description("Transforms a string value using the specified case format, optionally removing accents first",
	                "inflect('snake', 'helloWorld', true)", {"format", "text", "strip_accents"},
	                {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::BOOLEAN}, {"text", "case_conversion"});
	add_description("Transforms struct field names using the specified case format, optionally removing accents "
	                "from the names first",
	                "inflect('snake', {firstName: 'John'}, true)", {"format", "value", "strip_accents"},
	                {LogicalType::VARCHAR, LogicalType::ANY, LogicalType::BOOLEAN}, {"struct", "case_conversion"});

	loader.RegisterFunction(scalar_func_info);

	// --- Acronym configuration via DuckDB setting ---
	auto &db = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(db);
	config.AddExtensionOption(ACRONYMS_SETTING,
	                          "List of acronyms preserved as uppercase in case conversions (e.g., HTML, API). "
	                          "Session scoped: SET affects only the current connection, SET GLOBAL sets the default",
	                          LogicalType::LIST(LogicalType::VARCHAR), Value::LIST(LogicalType::VARCHAR, vector<Value>()));

	QueryFarmSendTelemetry(loader, "inflector", INFLECTOR_EXTENSION_VERSION);
}

void InflectorExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string InflectorExtension::Name() {
	return "inflector";
}

std::string InflectorExtension::Version() const {
	return INFLECTOR_EXTENSION_VERSION;
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(inflector, loader) {
	duckdb::LoadInternal(loader);
}
}
