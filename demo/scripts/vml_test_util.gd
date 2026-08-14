class_name VMLTestUtil
## Shared assertion helper for the headless regression suite.
## Usage: `if not VMLTestUtil.expect(cond, "T3 ..."): failed = true`

static func expect(p_cond: bool, p_name: String) -> bool:
	if p_cond:
		print("  PASS  ", p_name)
		return true
	push_error("  FAIL  " + p_name)
	return false


static func expect_eq(p_actual: Variant, p_expected: Variant, p_name: String) -> bool:
	if p_actual == p_expected:
		print("  PASS  ", p_name)
		return true
	push_error("  FAIL  " + p_name + " (got " + str(p_actual) + ", want " + str(p_expected) + ")")
	return false
