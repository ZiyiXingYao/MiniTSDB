#include <gtest/gtest.h>
#include "sql/parser.h"
#include "sql/ast.h"
using namespace minitsdb;
TEST(X, T6) { SQLParser p; auto r = p.Parse("CREATE ALARM a ON t WHEN v>1 THEN ACTION(\x27l\x27)"); ASSERT_TRUE(r.ok); }
