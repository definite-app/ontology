#define DUCKDB_EXTENSION_MAIN

#include "quack_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include <duckdb/parser/statement/select_statement.hpp>
#include <duckdb/parser/query_node/select_node.hpp>
#include <duckdb/parser/tableref/basetableref.hpp>
#include <nlohmann/json.hpp>

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

namespace duckdb {

using json = nlohmann::json;

// Dataset Registry Implementation
DatasetRegistry &DatasetRegistry::GetInstance() {
	static DatasetRegistry instance;
	return instance;
}

void DatasetRegistry::RegisterDataset(const string &name, const vector<SemanticMeasure> &measures,
                                      const vector<SemanticDimension> &dimensions) {
	dataset_measures_[name] = measures;
	dataset_dimensions_[name] = dimensions;
}

bool DatasetRegistry::ValidateQuery(const SemanticQuery &query, string &error_msg) {
	// Check if dataset exists
	if (dataset_measures_.find(query.dataset) == dataset_measures_.end()) {
		error_msg = "Dataset '" + query.dataset + "' not found in registry";
		return false;
	}

	auto &measures = dataset_measures_[query.dataset];
	auto &dimensions = dataset_dimensions_[query.dataset];

	// Validate measures
	for (const auto &measure_name : query.measures) {
		bool found = false;
		for (const auto &measure : measures) {
			if (measure.name == measure_name) {
				found = true;
				break;
			}
		}
		if (!found) {
			error_msg = "Measure '" + measure_name + "' not found in dataset '" + query.dataset + "'";
			return false;
		}
	}

	// Validate dimensions
	for (const auto &dimension_name : query.dimensions) {
		bool found = false;
		for (const auto &dimension : dimensions) {
			if (dimension.name == dimension_name) {
				found = true;
				break;
			}
		}
		if (!found) {
			error_msg = "Dimension '" + dimension_name + "' not found in dataset '" + query.dataset + "'";
			return false;
		}
	}

	// Validate time dimensions
	for (const auto &time_dim : query.time_dimensions) {
		bool found = false;
		for (const auto &dimension : dimensions) {
			if (dimension.name == time_dim.dimension) {
				found = true;
				break;
			}
		}
		if (!found) {
			error_msg = "Time dimension '" + time_dim.dimension + "' not found in dataset '" + query.dataset + "'";
			return false;
		}
	}

	return true;
}

const vector<SemanticMeasure> *DatasetRegistry::GetMeasures(const string &dataset_name) {
	auto it = dataset_measures_.find(dataset_name);
	return it != dataset_measures_.end() ? &it->second : nullptr;
}

const vector<SemanticDimension> *DatasetRegistry::GetDimensions(const string &dataset_name) {
	auto it = dataset_dimensions_.find(dataset_name);
	return it != dataset_dimensions_.end() ? &it->second : nullptr;
}

// JSON Parsing Functions
SemanticQuery ParseSemanticQuery(const string &json_str) {
	SemanticQuery query;

	try {
		json j = json::parse(json_str);

		// Parse dataset
		if (j.contains("dataset")) {
			query.dataset = j["dataset"].get<string>();
		}

		// Parse measures
		if (j.contains("measures")) {
			for (const auto &measure : j["measures"]) {
				query.measures.push_back(measure.get<string>());
			}
		}

		// Parse dimensions
		if (j.contains("dimensions")) {
			for (const auto &dimension : j["dimensions"]) {
				query.dimensions.push_back(dimension.get<string>());
			}
		}

		// Parse filters
		if (j.contains("filters")) {
			for (const auto &filter_json : j["filters"]) {
				SemanticFilter filter;
				filter.dimension = filter_json["dimension"].get<string>();
				filter.operator_ = filter_json["operator"].get<string>();
				for (const auto &value : filter_json["values"]) {
					filter.values.push_back(value.get<string>());
				}
				query.filters.push_back(filter);
			}
		}

		// Parse time_dimensions
		if (j.contains("time_dimensions")) {
			for (const auto &time_dim_json : j["time_dimensions"]) {
				SemanticTimeDimension time_dim;
				time_dim.dimension = time_dim_json["dimension"].get<string>();
				if (time_dim_json.contains("granularity")) {
					time_dim.granularity = time_dim_json["granularity"].get<string>();
				}
				if (time_dim_json.contains("date_range")) {
					for (const auto &date : time_dim_json["date_range"]) {
						time_dim.date_range.push_back(date.get<string>());
					}
				}
				query.time_dimensions.push_back(time_dim);
			}
		}

		// Parse order
		if (j.contains("order")) {
			for (const auto &order_json : j["order"]) {
				SemanticOrder order;
				order.id = order_json["id"].get<string>();
				order.desc = order_json.contains("desc") ? order_json["desc"].get<bool>() : false;
				query.order.push_back(order);
			}
		}

		// Parse limit
		if (j.contains("limit")) {
			query.limit = j["limit"].get<int64_t>();
		} else {
			query.limit = -1; // No limit
		}

		// Parse time_zone
		if (j.contains("time_zone")) {
			query.time_zone = j["time_zone"].get<string>();
		}

	} catch (const json::exception &e) {
		throw InvalidInputException("Invalid JSON in semantic query: " + string(e.what()));
	}

	return query;
}

// SQL Compilation Function
string CompileSemanticQueryToSQL(const SemanticQuery &query) {
	string sql = "SELECT ";

	auto &registry = DatasetRegistry::GetInstance();
	auto measures = registry.GetMeasures(query.dataset);
	auto dimensions = registry.GetDimensions(query.dataset);

	if (!measures || !dimensions) {
		throw InvalidInputException("Dataset '" + query.dataset + "' not found in registry");
	}

	vector<string> select_list;

	// Add measures
	for (const auto &measure_name : query.measures) {
		for (const auto &measure : *measures) {
			if (measure.name == measure_name) {
				select_list.push_back(measure.sql_expression + " AS " + measure.name);
				break;
			}
		}
	}

	// Add dimensions
	for (const auto &dimension_name : query.dimensions) {
		for (const auto &dimension : *dimensions) {
			if (dimension.name == dimension_name) {
				select_list.push_back(dimension.sql_expression + " AS " + dimension.name);
				break;
			}
		}
	}

	// Add time dimensions with granularity
	for (const auto &time_dim : query.time_dimensions) {
		for (const auto &dimension : *dimensions) {
			if (dimension.name == time_dim.dimension) {
				string time_expr = dimension.sql_expression;
				if (!time_dim.granularity.empty()) {
					if (time_dim.granularity == "day") {
						time_expr = "DATE_TRUNC('day', " + time_expr + ")";
					} else if (time_dim.granularity == "month") {
						time_expr = "DATE_TRUNC('month', " + time_expr + ")";
					} else if (time_dim.granularity == "year") {
						time_expr = "DATE_TRUNC('year', " + time_expr + ")";
					}
				}
				select_list.push_back(time_expr + " AS " + time_dim.dimension);
				break;
			}
		}
	}

	if (select_list.empty()) {
		throw InvalidInputException("No valid measures or dimensions specified");
	}

	sql += StringUtil::Join(select_list, ", ");
	sql += " FROM " + query.dataset;

	// Add WHERE clause
	vector<string> where_conditions;

	// Add regular filters
	for (const auto &filter : query.filters) {
		if (filter.operator_ == "equals") {
			if (filter.values.size() == 1) {
				where_conditions.push_back(filter.dimension + " = '" + filter.values[0] + "'");
			} else if (filter.values.size() > 1) {
				vector<string> quoted_values;
				for (const auto &value : filter.values) {
					quoted_values.push_back("'" + value + "'");
				}
				where_conditions.push_back(filter.dimension + " IN (" + StringUtil::Join(quoted_values, ", ") + ")");
			}
		} else if (filter.operator_ == "not_equals") {
			if (filter.values.size() == 1) {
				where_conditions.push_back(filter.dimension + " != '" + filter.values[0] + "'");
			} else if (filter.values.size() > 1) {
				vector<string> quoted_values;
				for (const auto &value : filter.values) {
					quoted_values.push_back("'" + value + "'");
				}
				where_conditions.push_back(filter.dimension + " NOT IN (" + StringUtil::Join(quoted_values, ", ") +
				                           ")");
			}
		}
	}

	// Add time dimension filters
	for (const auto &time_dim : query.time_dimensions) {
		if (time_dim.date_range.size() == 2) {
			where_conditions.push_back(time_dim.dimension + " >= '" + time_dim.date_range[0] + "'");
			where_conditions.push_back(time_dim.dimension + " <= '" + time_dim.date_range[1] + "'");
		}
	}

	if (!where_conditions.empty()) {
		sql += " WHERE " + StringUtil::Join(where_conditions, " AND ");
	}

	// Add GROUP BY clause (if we have measures)
	if (!query.measures.empty()) {
		vector<string> group_by_list;
		for (const auto &dimension_name : query.dimensions) {
			group_by_list.push_back(dimension_name);
		}
		for (const auto &time_dim : query.time_dimensions) {
			group_by_list.push_back(time_dim.dimension);
		}
		if (!group_by_list.empty()) {
			sql += " GROUP BY " + StringUtil::Join(group_by_list, ", ");
		}
	}

	// Add ORDER BY clause
	if (!query.order.empty()) {
		vector<string> order_list;
		for (const auto &order : query.order) {
			string order_clause = order.id;
			if (order.desc) {
				order_clause += " DESC";
			}
			order_list.push_back(order_clause);
		}
		sql += " ORDER BY " + StringUtil::Join(order_list, ", ");
	}

	// Add LIMIT clause
	if (query.limit > 0) {
		sql += " LIMIT " + to_string(query.limit);
	}

	return sql;
}

// Table Function Data Structure
struct SemanticQueryData : public TableFunctionData {
	string query_json;
	string compiled_sql;
	bool explained;
	bool finished;

	SemanticQueryData(string json, bool explain = false) : query_json(json), explained(explain), finished(false) {
		auto semantic_query = ParseSemanticQuery(json);
		string error_msg;
		if (!DatasetRegistry::GetInstance().ValidateQuery(semantic_query, error_msg)) {
			throw InvalidInputException("Semantic query validation failed: " + error_msg);
		}
		compiled_sql = CompileSemanticQueryToSQL(semantic_query);
	}
};

// Table Function Implementation
static unique_ptr<FunctionData> SemanticQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                                  vector<LogicalType> &return_types, vector<string> &names) {
	if (input.inputs.empty()) {
		throw InvalidInputException("SEMANTIC_QUERY requires at least one argument (JSON query)");
	}

	bool explain_mode = false;
	if (input.inputs.size() > 1) {
		// Check if second parameter indicates explain mode
		if (input.inputs[1].type() == LogicalType::BOOLEAN) {
			explain_mode = input.inputs[1].GetValue<bool>();
		}
	}

	string query_json = input.inputs[0].GetValue<string>();
	auto data = make_uniq<SemanticQueryData>(query_json, explain_mode);

	if (explain_mode) {
		// For EXPLAIN mode, return the compiled SQL
		return_types = {LogicalType::VARCHAR};
		names = {"compiled_sql"};
	} else {
		// For normal mode, return a simplified schema
		// In production, this would be dynamically determined from the query
		return_types = {LogicalType::VARCHAR, LogicalType::BIGINT, LogicalType::DATE};
		names = {"result", "count", "date"};
	}

	return std::move(data);
}

static void SemanticQueryFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &data = (SemanticQueryData &)*data_p.bind_data;

	// If already finished, return empty chunk
	if (data.finished) {
		output.SetCardinality(0);
		return;
	}

	if (data.explained) {
		// Return the compiled SQL for explain mode
		output.SetCardinality(1);
		output.SetValue(0, 0, Value(data.compiled_sql));
		data.finished = true;
	} else {
		// For normal mode, return a placeholder result showing the compiled SQL
		// In a production implementation, this would execute the SQL and return actual data
		output.SetCardinality(1);
		output.SetValue(0, 0, Value("Compiled SQL: " + data.compiled_sql));
		output.SetValue(1, 0, Value::BIGINT(1));
		output.SetValue(2, 0, Value::DATE(Date::FromString("2025-01-01")));
		data.finished = true;
	}
}

// Scalar Functions (existing)
inline void QuackScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(result, "Quack " + name.GetString() + " 🐥");
	});
}

inline void QuackOpenSSLVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(result, "Quack " + name.GetString() + ", my linked OpenSSL version is " +
		                                           OPENSSL_VERSION_TEXT);
	});
}

// Dataset Registration Function
inline void RegisterDatasetScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &dataset_name_vector = args.data[0];
	auto &dataset_json_vector = args.data[1];

	BinaryExecutor::Execute<string_t, string_t, string_t>(
	    dataset_name_vector, dataset_json_vector, result, args.size(),
	    [&](string_t dataset_name, string_t dataset_json) {
		    try {
			    // Parse the complete dataset JSON
			    json dataset_j = json::parse(dataset_json.GetString());

			    // Parse measures
			    vector<SemanticMeasure> measures;
			    if (dataset_j.contains("measures")) {
				    for (const auto &measure_json : dataset_j["measures"]) {
					    SemanticMeasure measure;
					    measure.name = measure_json["name"].get<string>();
					    measure.aggregation_type =
					        measure_json.contains("type") ? measure_json["type"].get<string>() : "sum";
					    measure.sql_expression = measure_json["sql"].get<string>();
					    measures.push_back(measure);
				    }
			    }

			    // Parse dimensions
			    vector<SemanticDimension> dimensions;
			    if (dataset_j.contains("dimensions")) {
				    for (const auto &dimension_json : dataset_j["dimensions"]) {
					    SemanticDimension dimension;
					    dimension.name = dimension_json["name"].get<string>();
					    dimension.sql_expression = dimension_json["sql"].get<string>();
					    dimension.data_type = LogicalType::VARCHAR; // Simplified
					    dimensions.push_back(dimension);
				    }
			    }

			    // Parse time_dimensions
			    if (dataset_j.contains("time_dimensions")) {
				    for (const auto &time_dim_json : dataset_j["time_dimensions"]) {
					    SemanticDimension dimension;
					    dimension.name = time_dim_json["name"].get<string>();
					    dimension.sql_expression = time_dim_json["sql"].get<string>();
					    dimension.data_type = LogicalType::DATE; // Time dimensions are date type
					    dimensions.push_back(dimension);
				    }
			    }

			    // Register the dataset
			    DatasetRegistry::GetInstance().RegisterDataset(dataset_name.GetString(), measures, dimensions);

			    return StringVector::AddString(result,
			                                   "Dataset '" + dataset_name.GetString() + "' registered successfully");
		    } catch (const std::exception &e) {
			    throw InvalidInputException("Failed to register dataset: " + string(e.what()));
		    }
	    });
}

void RegisterSemanticQueryFunctions(DatabaseInstance &instance) {
	// Register the table function
	TableFunction semantic_query_func("SEMANTIC_QUERY", {LogicalType::VARCHAR}, SemanticQueryFunction,
	                                  SemanticQueryBind);
	semantic_query_func.varargs = LogicalType::ANY;
	ExtensionUtil::RegisterFunction(instance, semantic_query_func);

	// Register dataset registration function
	auto register_dataset_function = ScalarFunction("REGISTER_DATASET", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                                LogicalType::VARCHAR, RegisterDatasetScalarFun);
	ExtensionUtil::RegisterFunction(instance, register_dataset_function);
}

static void LoadInternal(DatabaseInstance &instance) {
	// Register original scalar functions
	auto quack_scalar_function = ScalarFunction("quack", {LogicalType::VARCHAR}, LogicalType::VARCHAR, QuackScalarFun);
	ExtensionUtil::RegisterFunction(instance, quack_scalar_function);

	auto quack_openssl_version_scalar_function = ScalarFunction("quack_openssl_version", {LogicalType::VARCHAR},
	                                                            LogicalType::VARCHAR, QuackOpenSSLVersionScalarFun);
	ExtensionUtil::RegisterFunction(instance, quack_openssl_version_scalar_function);

	// Register semantic query functions
	RegisterSemanticQueryFunctions(instance);
}

void QuackExtension::Load(DuckDB &db) {
	LoadInternal(*db.instance);
}

std::string QuackExtension::Name() {
	return "quack";
}

std::string QuackExtension::Version() const {
#ifdef EXT_VERSION_QUACK
	return EXT_VERSION_QUACK;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void quack_init(duckdb::DatabaseInstance &db) {
	duckdb::DuckDB db_wrapper(db);
	db_wrapper.LoadExtension<duckdb::QuackExtension>();
}

DUCKDB_EXTENSION_API const char *quack_version() {
	return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
