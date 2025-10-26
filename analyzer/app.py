from flask import Flask, request, jsonify
import math

app = Flask(__name__)

def solve_recurrence(a, b, f_expr):
    # Estimate the order of growth of f(n)
    # Supported f_expr: "n", "n^k", "n log n", "log n", "1"
    f_expr = f_expr.strip().lower()

    # Estimate f(n) order
    if f_expr in ["1", "constant"]:
        f_order = 0
        f_label = "Θ(1)"
    elif f_expr == "log n":
        f_order = "logn"
        f_label = "Θ(log n)"
    elif f_expr == "n":
        f_order = 1
        f_label = "Θ(n)"
    elif f_expr.startswith("n^"):
        try:
            k = float(f_expr[2:])
            f_order = k
            f_label = f"Θ(n^{k})"
        except ValueError:
            return {"error": f"Unsupported function f(n): {f_expr}"}
    elif f_expr == "n log n":
        f_order = "nlogn"
        f_label = "Θ(n log n)"
    else:
        return {"error": f"Unsupported function f(n): {f_expr}"}

    try:
        log_ab = math.log(a, b)
    except:
        return {"error": "Invalid values for a and b"}

    # Apply Master Theorem
    reasoning = []
    reasoning.append(f"T(n) = {a}T(n/{b}) + {f_label}")
    reasoning.append(f"log_b(a) = log_{b}({a}) = {log_ab:.2f}")

    if f_order == "logn":
        result = f"O(n^{log_ab:.2f})"
        reasoning.append("Case 1: f(n) = o(n^log_b(a))")
    elif f_order == "nlogn":
        result = f"O(n^{log_ab:.2f} log n)"
        reasoning.append("Case 2 (tight bound): f(n) = Θ(n^log_b(a) * log n)")
    elif isinstance(f_order, (int, float)):
        if f_order < log_ab:
            result = f"O(n^{log_ab:.2f})"
            reasoning.append("Case 1: f(n) = o(n^log_b(a))")
        elif f_order == log_ab:
            result = f"O(n^{f_order} log n)"
            reasoning.append("Case 2: f(n) = Θ(n^log_b(a))")
        else:
            result = f"O(n^{f_order})"
            reasoning.append("Case 3: f(n) = Ω(n^log_b(a)) and regularity satisfied")
    else:
        return {"error": f"Unsupported f(n) format: {f_expr}"}

    return {
        "recurrence": f"T(n) = {a}T(n/{b}) + {f_expr}",
        "solution": result,
        "case_reasoning": reasoning
    }

@app.route("/analyze", methods=["POST"])
def analyze():
    data = request.get_json(silent=True)
    if not isinstance(data, dict) or "summary" not in data:
        return jsonify({"error": "invalid input"}), 400

    summary = data["summary"]
    loops = summary.get("loops", [])
    functions = summary.get("functions", [])

    max_depth = max((loop.get("depth", 1) for loop in loops), default=0)
    is_recursive = any(f.get("is_recursive", False) for f in functions)

    complexity = f"O(n^{max_depth})" if max_depth > 0 else "O(1)"
    if is_recursive:
        complexity += " (recursive function detected)"

    explanation = [
        f"Detected {len(loops)} loops; max depth = {max_depth}",
        ("recursive functions present in: " +
         ", ".join(f["name"] for f in functions if f.get("is_recursive")))
        if is_recursive else "no recursive functions detected"
    ]

    # -------- NEW: extract recurrence from several accepted locations --------
    recur = None
    src = None

    # 1) Top-level
    if isinstance(data.get("recurrence"), dict):
        recur = data["recurrence"]; src = "recurrence"

    # 2) summary.recurrence
    if recur is None and isinstance(summary.get("recurrence"), dict):
        recur = summary["recurrence"]; src = "summary.recurrence"

    # 3) summary.recurrences (only if exactly one)
    if recur is None and isinstance(summary.get("recurrences"), list):
        recs = [r for r in summary["recurrences"] if isinstance(r, dict)]
        if len(recs) == 1:
            recur = recs[0]; src = "summary.recurrences[0]"

    # 4) functions[*].recurrence (only if exactly one)
    if recur is None and isinstance(functions, list):
        fn_recs = [(i, f.get("recurrence")) for i, f in enumerate(functions)
                   if isinstance(f, dict) and isinstance(f.get("recurrence"), dict)]
        if len(fn_recs) == 1:
            idx, rdict = fn_recs[0]
            recur = rdict; src = f"summary.functions[{idx}].recurrence"

    recurrence_output = None
    a = b = f_expr = None
    if isinstance(recur, dict):
        a = recur.get("a"); b = recur.get("b"); f_expr = recur.get("f")
        # Only solve Master Theorem when a, b, f are all present
        if a is not None and b is not None and f_expr:
            recurrence_output = solve_recurrence(a, b, f_expr)

    # -------- NEW: promote recurrence solution to main complexity --------
    if isinstance(recurrence_output, dict) and "solution" in recurrence_output:
        complexity = recurrence_output["solution"]
        lead = (f"Solved via Master Theorem from recurrence a={a}, b={b}, f(n)={f_expr}"
                + (f" (from {src})" if src else ""))
        explanation = [lead, *explanation]

    result = {
        "complexity": complexity,
        "explanation": explanation
    }
    if recurrence_output:
        result["recurrence_solution"] = recurrence_output

    return jsonify(result)


