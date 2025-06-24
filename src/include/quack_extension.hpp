#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

// Forward declarations
struct SemanticQueryData;
struct DatasetRegistry;

// Semantic Query API structures
struct SemanticMeasure {
	string name;
	string aggregation_type;
	string sql_expression;
};

struct SemanticDimension {
	string name;
	string sql_expression;
	LogicalType data_type;
};

struct SemanticFilter {
	string dimension;
	string operator_;
	vector<string> values;
};

struct SemanticTimeDimension {
	string dimension;
	string granularity;
	vector<string> date_range;
};

struct SemanticOrder {
	string id;
	bool desc;
};

struct SemanticQuery {
	string dataset;
	vector<string> measures;
	vector<string> dimensions;
	vector<SemanticFilter> filters;
	vector<SemanticTimeDimension> time_dimensions;
	vector<SemanticOrder> order;
	int64_t limit;
	string time_zone;
};

// Dataset registry for validation
class DatasetRegistry {
public:
	static DatasetRegistry &GetInstance();
	void RegisterDataset(const string &name, const vector<SemanticMeasure> &measures,
	                     const vector<SemanticDimension> &dimensions);
	bool ValidateQuery(const SemanticQuery &query, string &error_msg);
	const vector<SemanticMeasure> *GetMeasures(const string &dataset_name);
	const vector<SemanticDimension> *GetDimensions(const string &dataset_name);

private:
	unordered_map<string, vector<SemanticMeasure>> dataset_measures_;
	unordered_map<string, vector<SemanticDimension>> dataset_dimensions_;
};

class QuackExtension : public Extension {
public:
	void Load(DuckDB &db) override;
	std::string Name() override;
	std::string Version() const override;
};

// Semantic Query API functions
SemanticQuery ParseSemanticQuery(const string &json_str);
string CompileSemanticQueryToSQL(const SemanticQuery &query);
void RegisterSemanticQueryFunctions(DatabaseInstance &instance);

} // namespace duckdb
