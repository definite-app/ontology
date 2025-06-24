#!/usr/bin/env python3
"""
Comprehensive test script for the Semantic Query API extension.
Tests all functionality described in the PRD including snake_case JSON support.
"""

import duckdb
import json
import sys
import traceback
from typing import Dict, List, Any

class SemanticQueryTester:
    def __init__(self):
        self.conn = None
        self.test_results = []
        self.failed_tests = []
        
    def setup(self):
        """Initialize DuckDB connection and load the extension."""
        try:
            self.conn = duckdb.connect()
            # Try to load the extension (assuming it's built and available)
            self.conn.execute("LOAD 'build/release/extension/quack/quack.duckdb_extension'")
            print("✓ Extension loaded successfully")
            return True
        except Exception as e:
            print(f"✗ Failed to load extension: {e}")
            return False
    
    def run_test(self, test_name: str, test_func):
        """Run a single test and record results."""
        try:
            print(f"Running {test_name}...")
            test_func()
            print(f"✓ {test_name} passed")
            self.test_results.append({"name": test_name, "status": "PASSED"})
        except Exception as e:
            print(f"✗ {test_name} failed: {e}")
            traceback.print_exc()
            self.test_results.append({"name": test_name, "status": "FAILED", "error": str(e)})
            self.failed_tests.append(test_name)
    
    def test_dataset_registration(self):
        """Test dataset registration functionality."""
        # Register orders dataset
        measures_json = json.dumps([
            {"name": "orders_ds.total_revenue", "sql_expression": "SUM(order_amount)", "aggregation_type": "sum"},
            {"name": "orders_ds.order_count", "sql_expression": "COUNT(*)", "aggregation_type": "count"},
            {"name": "orders_ds.avg_order", "sql_expression": "AVG(order_amount)", "aggregation_type": "avg"}
        ])
        
        dimensions_json = json.dumps([
            {"name": "orders_ds.order_date", "sql_expression": "order_date"},
            {"name": "orders_ds.customer_id", "sql_expression": "customer_id"},
            {"name": "orders_ds.product_category", "sql_expression": "product_category"}
        ])
        
        result = self.conn.execute(
            "SELECT REGISTER_DATASET(?, ?, ?)",
            ['orders_ds', measures_json, dimensions_json]
        ).fetchone()
        
        assert result[0] == "Dataset 'orders_ds' registered successfully"
        
        # Register sales dataset
        sales_measures = json.dumps([
            {"name": "sales_ds.revenue", "sql_expression": "SUM(sales_amount)", "aggregation_type": "sum"},
            {"name": "sales_ds.avg_sale", "sql_expression": "AVG(sales_amount)", "aggregation_type": "avg"}
        ])
        
        sales_dimensions = json.dumps([
            {"name": "sales_ds.sale_date", "sql_expression": "sale_date"},
            {"name": "sales_ds.product_id", "sql_expression": "product_id"},
            {"name": "sales_ds.region", "sql_expression": "region"}
        ])
        
        result = self.conn.execute(
            "SELECT REGISTER_DATASET(?, ?, ?)",
            ['sales_ds', sales_measures, sales_dimensions]
        ).fetchone()
        
        assert result[0] == "Dataset 'sales_ds' registered successfully"
    
    def test_basic_semantic_query(self):
        """Test basic semantic query with measures only."""
        query = {
            "dataset": "orders_ds",
            "measures": ["orders_ds.total_revenue"]
        }
        
        # Should not throw an error
        result = self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
        # Result should be empty (no actual data) but query should compile
    
    def test_measures_and_dimensions(self):
        """Test semantic query with both measures and dimensions."""
        query = {
            "dataset": "orders_ds",
            "measures": ["orders_ds.total_revenue", "orders_ds.order_count"],
            "dimensions": ["orders_ds.customer_id", "orders_ds.product_category"]
        }
        
        result = self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
    
    def test_time_dimensions_snake_case(self):
        """Test time_dimensions with snake_case JSON format."""
        query = {
            "dataset": "orders_ds",
            "measures": ["orders_ds.total_revenue"],
            "time_dimensions": [
                {
                    "dimension": "orders_ds.order_date",
                    "granularity": "day",
                    "date_range": ["2025-01-01", "2025-01-31"]
                }
            ]
        }
        
        result = self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
    
    def test_filters(self):
        """Test various filter types."""
        # Test equals filter
        query = {
            "dataset": "orders_ds",
            "measures": ["orders_ds.total_revenue"],
            "dimensions": ["orders_ds.customer_id"],
            "filters": [
                {
                    "dimension": "orders_ds.customer_id",
                    "operator": "equals",
                    "values": ["123", "456"]
                }
            ]
        }
        
        result = self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
        
        # Test not_equals filter
        query["filters"][0]["operator"] = "not_equals"
        result = self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
    
    def test_order_and_limit(self):
        """Test order and limit functionality."""
        query = {
            "dataset": "orders_ds",
            "measures": ["orders_ds.total_revenue"],
            "dimensions": ["orders_ds.customer_id"],
            "order": [
                {"id": "orders_ds.total_revenue", "desc": True},
                {"id": "orders_ds.customer_id", "desc": False}
            ],
            "limit": 10
        }
        
        result = self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
    
    def test_complex_query(self):
        """Test complex query with all features."""
        query = {
            "dataset": "orders_ds",
            "measures": ["orders_ds.total_revenue", "orders_ds.order_count"],
            "dimensions": ["orders_ds.customer_id"],
            "time_dimensions": [
                {
                    "dimension": "orders_ds.order_date",
                    "granularity": "month",
                    "date_range": ["2025-01-01", "2025-12-31"]
                }
            ],
            "filters": [
                {
                    "dimension": "orders_ds.product_category",
                    "operator": "equals",
                    "values": ["Electronics", "Books"]
                }
            ],
            "order": [
                {"id": "orders_ds.order_date", "desc": False},
                {"id": "orders_ds.total_revenue", "desc": True}
            ],
            "limit": 100,
            "time_zone": "UTC"
        }
        
        result = self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
    
    def test_explain_functionality(self):
        """Test EXPLAIN functionality that returns compiled SQL."""
        query = {
            "dataset": "orders_ds",
            "measures": ["orders_ds.total_revenue"],
            "dimensions": ["orders_ds.customer_id"]
        }
        
        result = self.conn.execute(
            "SELECT * FROM SEMANTIC_QUERY(?, ?)", 
            [json.dumps(query), True]
        ).fetchone()
        
        # Should return compiled SQL
        compiled_sql = result[0]
        assert "SELECT" in compiled_sql
        assert "orders_ds" in compiled_sql
        assert "SUM(order_amount)" in compiled_sql
        print(f"  Compiled SQL: {compiled_sql}")
    
    def test_granularity_options(self):
        """Test different time granularity options."""
        granularities = ["day", "month", "year"]
        
        for granularity in granularities:
            query = {
                "dataset": "orders_ds",
                "measures": ["orders_ds.total_revenue"],
                "time_dimensions": [
                    {
                        "dimension": "orders_ds.order_date",
                        "granularity": granularity
                    }
                ]
            }
            
            result = self.conn.execute(
                "SELECT * FROM SEMANTIC_QUERY(?, ?)", 
                [json.dumps(query), True]
            ).fetchone()
            
            compiled_sql = result[0]
            assert f"DATE_TRUNC('{granularity}'" in compiled_sql
            print(f"  {granularity.capitalize()} granularity SQL: {compiled_sql}")
    
    def test_multiple_datasets(self):
        """Test that multiple datasets can coexist."""
        # Query orders dataset
        orders_query = {
            "dataset": "orders_ds",
            "measures": ["orders_ds.total_revenue"]
        }
        
        result1 = self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(orders_query)]).fetchall()
        
        # Query sales dataset
        sales_query = {
            "dataset": "sales_ds",
            "measures": ["sales_ds.revenue"],
            "dimensions": ["sales_ds.region"]
        }
        
        result2 = self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(sales_query)]).fetchall()
    
    def test_error_cases(self):
        """Test various error conditions."""
        # Invalid dataset
        try:
            query = {"dataset": "nonexistent_ds", "measures": ["some_measure"]}
            self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
            assert False, "Should have thrown error for invalid dataset"
        except Exception as e:
            assert "not found in registry" in str(e)
        
        # Invalid measure
        try:
            query = {"dataset": "orders_ds", "measures": ["invalid_measure"]}
            self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
            assert False, "Should have thrown error for invalid measure"
        except Exception as e:
            assert "not found in dataset" in str(e)
        
        # Invalid dimension
        try:
            query = {"dataset": "orders_ds", "dimensions": ["invalid_dimension"]}
            self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
            assert False, "Should have thrown error for invalid dimension"
        except Exception as e:
            assert "not found in dataset" in str(e)
        
        # Invalid JSON
        try:
            self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", ["invalid json"]).fetchall()
            assert False, "Should have thrown error for invalid JSON"
        except Exception as e:
            assert "Invalid JSON" in str(e)
        
        # Empty query
        try:
            query = {"dataset": "orders_ds"}
            self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
            assert False, "Should have thrown error for empty query"
        except Exception as e:
            assert "No valid measures or dimensions" in str(e)
    
    def test_snake_case_compliance(self):
        """Test that all JSON fields use snake_case as per PRD."""
        # This query uses all snake_case fields
        query = {
            "dataset": "orders_ds",
            "measures": ["orders_ds.total_revenue"],
            "dimensions": ["orders_ds.customer_id"],
            "time_dimensions": [  # snake_case instead of timeDimensions
                {
                    "dimension": "orders_ds.order_date",
                    "granularity": "day",
                    "date_range": ["2025-01-01", "2025-01-31"]  # snake_case instead of dateRange
                }
            ],
            "filters": [
                {
                    "dimension": "orders_ds.customer_id",
                    "operator": "equals",
                    "values": ["123"]
                }
            ],
            "order": [
                {"id": "orders_ds.customer_id", "desc": False}
            ],
            "limit": 10,
            "time_zone": "UTC"  # snake_case instead of timeZone
        }
        
        # Should parse successfully with snake_case
        result = self.conn.execute("SELECT * FROM SEMANTIC_QUERY(?)", [json.dumps(query)]).fetchall()
    
    def test_original_functions(self):
        """Test that original quack functions still work."""
        result = self.conn.execute("SELECT quack('Semantic')").fetchone()
        assert result[0] == "Quack Semantic 🐥"
        
        result = self.conn.execute("SELECT quack_openssl_version('Test')").fetchone()
        assert "OpenSSL" in result[0]
    
    def run_all_tests(self):
        """Run all tests and report results."""
        print("=" * 60)
        print("SEMANTIC QUERY API TEST SUITE")
        print("=" * 60)
        
        if not self.setup():
            return False
        
        # Run all tests
        tests = [
            ("Dataset Registration", self.test_dataset_registration),
            ("Basic Semantic Query", self.test_basic_semantic_query),
            ("Measures and Dimensions", self.test_measures_and_dimensions),
            ("Time Dimensions (snake_case)", self.test_time_dimensions_snake_case),
            ("Filters", self.test_filters),
            ("Order and Limit", self.test_order_and_limit),
            ("Complex Query", self.test_complex_query),
            ("EXPLAIN Functionality", self.test_explain_functionality),
            ("Granularity Options", self.test_granularity_options),
            ("Multiple Datasets", self.test_multiple_datasets),
            ("Error Cases", self.test_error_cases),
            ("Snake Case Compliance", self.test_snake_case_compliance),
            ("Original Functions", self.test_original_functions),
        ]
        
        for test_name, test_func in tests:
            self.run_test(test_name, test_func)
        
        # Print summary
        print("\n" + "=" * 60)
        print("TEST SUMMARY")
        print("=" * 60)
        
        passed = len([t for t in self.test_results if t["status"] == "PASSED"])
        failed = len([t for t in self.test_results if t["status"] == "FAILED"])
        total = len(self.test_results)
        
        print(f"Total tests: {total}")
        print(f"Passed: {passed}")
        print(f"Failed: {failed}")
        
        if failed > 0:
            print(f"\nFailed tests:")
            for test_name in self.failed_tests:
                print(f"  - {test_name}")
        
        print(f"\nSuccess rate: {(passed/total)*100:.1f}%")
        
        return failed == 0

def main():
    """Main entry point."""
    tester = SemanticQueryTester()
    success = tester.run_all_tests()
    
    if success:
        print("\n🎉 All tests passed!")
        sys.exit(0)
    else:
        print("\n❌ Some tests failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()