from flask import Flask, request, jsonify
import math

app = Flask(__name__)

# ==============================
# Small, clear helpers
# ==============================

def fmt_exp(x: float) -> str:
    """Format exponents nicely: 1.00 -> 1"""
    if abs(x - round(x)) < 1e-9:
        return str(int(round(x)))
    return f"{x:.2f}"

def classify_f(expr: str):
    """
    Map f(n) string -> a simple class:
      Returns: (kind, param, label)
        - ("const", None, "Θ(1)")
        - ("log", None, "Θ(log n)")
        - ("n", None, "Θ(n)")
        - ("nk", k, "Θ(n^k)")
        - ("nlogn", None, "Θ(n log n)")
        - ("unsupported", None, raw)
    """
    raw = (expr or "").strip()
    f = raw.lower().strip()

    if f in ("1", "constant"):
        return "const", None, "Θ(1)"
    if f == "log n":
        return "log", None, "Θ(log n)"
    if f == "n":
        return "n", None, "Θ(n)"
    if f.startswith("n^"):
        try:
            k = float(f[2:])
            return "nk", k, f"Θ(n^{fmt_exp(k)})"
        except ValueError:
            return "unsupported", None, raw
    if f == "n log n":
        return "nlogn", None, "Θ(n log n)"
    return "unsupported", None, raw

def compare_growth(current_kind, current_k, inferred_kind, inferred_k) -> bool:
    """
    Return True if inferred f(n) is *asymptotically stronger* than current f(n).
    Very small comparator to keep it simple:
      Order by polynomial degree first, then log factor presence.
    """
    def to_rank(kind, k):
        # (poly_degree, has_log)
        if kind == "const":
            return (0.0, False)
        if kind == "log":
            return (0.0, True)   # log n < n^ε but mark as log
        if kind == "n":
            return (1.0, False)
        if kind == "nk":
            return (float(k), False)
        if kind == "nlogn":
            return (1.0, True)
        # unsupported => weakest
        return (0.0, False)

    p1, lg1 = to_rank(current_kind, current_k)
    p2, lg2 = to_rank(inferred_kind, inferred_k)

    if p2 > p1:
        return True
    if abs(p2 - p1) < 1e-9 and lg2 and not lg1:
        return True
    return False

def solve_master_theorem(a: float, b: float, f_expr: str):
    """
    Solve T(n) = a T(n/b) + f(n) for the standard Master Theorem cases.
    Supports: 1, constant, log n, n, n^k, n log n
    Returns dict with {recurrence, solution, case_reasoning} or {error: ...}
    """
    if not (isinstance(a, (int, float)) and isinstance(b, (int, float))):
        return {"error": "Invalid a or b."}
    if b <= 1:
        return {"error": "Master Theorem requires b > 1."}

    kind, k, f_label = classify_f(f_expr)
    if kind == "unsupported":
        return {"error": f"Unsupported f(n): {f_expr}"}

    log_ab = math.log(a, b)
    lines = [
        f"T(n) = {a}T(n/{b}) + {f_expr}",
        f"log_b(a) = log_{b}({a}) = {log_ab:.2f}"
    ]

    # Handle cases by comparing f(n) to n^{log_b a}
    if kind == "log":
        sol = f"O(n^{fmt_exp(log_ab)})"
        lines.append("Case 1: f(n) = o(n^{log_b(a)}).")
    elif kind == "nlogn":
        if abs(log_ab - 1.0) < 1e-9:
            sol = "O(n log n)"
            lines.append("Case 2: f(n) = Θ(n^{log_b(a)} · log n).")
        elif log_ab < 1.0:
            sol = "O(n log n)"
            lines.append("Case 3: f(n) = Ω(n^{log_b(a)}), regularity assumed.")
        else:
            sol = f"O(n^{fmt_exp(log_ab)})"
            lines.append("Case 1: n^{log_b(a)} dominates.")
    elif kind in ("const", "n", "nk"):
        f_pow = 0.0 if kind == "const" else (1.0 if kind == "n" else float(k))
        if f_pow < log_ab - 1e-9:
            sol = f"O(n^{fmt_exp(log_ab)})"
            lines.append("Case 1: f(n) = o(n^{log_b(a)}).")
        elif abs(f_pow - log_ab) <= 1e-9:
            sol = f"O(n^{fmt_exp(f_pow)} log n)"
            lines.append("Case 2: f(n) = Θ(n^{log_b(a)}).")
        else:
            sol = f"O(n^{fmt_exp(f_pow)})"
            lines.append("Case 3: f(n) = Ω(n^{log_b(a)}), regularity assumed.")
    else:
        return {"error": f"Unsupported f(n): {f_expr}"}

    return {
        "recurrence": f"T(n) = {a}T(n/{b}) + {f_expr}",
        "solution": sol,
        "case_reasoning": lines
    }

# ==============================
# Recurrence extraction & f(n) upgrade from callees
# ==============================

def extract_recurrence(doc: dict):
    """
    Look for a recurrence dict with keys a, b, f in a few obvious spots.
    Return (a, b, f, src_label, func_name) or None.
    """
    # top-level
    if isinstance(doc.get("recurrence"), dict):
        r = doc["recurrence"]
        if {"a", "b", "f"} <= r.keys():
            return r["a"], r["b"], r["f"], "recurrence", r.get("function")

    summary = doc.get("summary", {})
    # summary.recurrence
    if isinstance(summary.get("recurrence"), dict):
        r = summary["recurrence"]
        if {"a", "b", "f"} <= r.keys():
            return r["a"], r["b"], r["f"], "summary.recurrence", r.get("function")

    # summary.recurrences (exactly one)
    recs = [r for r in summary.get("recurrences", []) if isinstance(r, dict)]
    if len(recs) == 1 and {"a", "b", "f"} <= recs[0].keys():
        r = recs[0]
        func = r.get("function")
        return r["a"], r["b"], r["f"], "summary.recurrences[0]", func

    # functions[*].recurrence (exactly one)
    fns = summary.get("functions", [])
    fn_matches = []
    for f in fns:
        if isinstance(f, dict) and isinstance(f.get("recurrence"), dict):
            r = f["recurrence"]
            if {"a", "b", "f"} <= r.keys():
                fn_matches.append((f.get("name"), r))
    if len(fn_matches) == 1:
        name, r = fn_matches[0]
        return r["a"], r["b"], r["f"], "summary.functions[*].recurrence", name

    return None

def pick_recursive_function_name(summary: dict) -> str | None:
    """Choose a recursive function if there is exactly one."""
    funcs = [f for f in summary.get("functions", []) if isinstance(f, dict)]
    rec = [f.get("name") for f in funcs if f.get("is_recursive")]
    if len(rec) == 1:
        return rec[0]
    return None

def infer_per_level_work(summary: dict, func_name: str) -> str | None:
    """
    Infer per-level non-recursive work for a recursive function from its non-recursive callees.
    Returns an f(n) expression string: "1", "n", or "n^k".
    """
    if not func_name:
        return None

    funcs = {f.get("name"): f for f in summary.get("functions", []) if isinstance(f, dict)}
    F = funcs.get(func_name)
    if not F:
        return None

    callees = F.get("calls", []) or []
    inferred_degree = 0  # 0 => constant by default

    for callee_name in callees:
        G = funcs.get(callee_name)
        if not G or G.get("is_recursive"):
            continue  # only consider non-recursive helpers

        depth = G.get("maxLoopDepth", 0) or 0
        loop_count = G.get("loopCount", 0) or 0

        # If a helper has any loop nesting, model as n^depth (depth>=1 -> at least linear).
        if depth >= 1:
            inferred_degree = max(inferred_degree, int(depth))
            continue

        # If no depth reported but loops exist, conservatively treat as linear
        if depth == 0 and loop_count > 0:
            inferred_degree = max(inferred_degree, 1)

    if inferred_degree <= 0:
        return "1"
    if inferred_degree == 1:
        return "n"
    return f"n^{inferred_degree}"

def upgrade_f_if_weaker(provided_f: str, inferred_f: str) -> tuple[str, bool]:
    """
    If inferred_f is asymptotically stronger than provided_f, return inferred_f and True.
    Otherwise return provided_f and False.
    """
    pk, pkval, _ = classify_f(provided_f)
    ik, ikval, _ = classify_f(inferred_f)

    if pk == "unsupported":
        return inferred_f, True
    if ik == "unsupported":
        return provided_f, False

    if compare_growth(pk, pkval, ik, ikval):
        return inferred_f, True
    return provided_f, False

# ==============================
# Loop baseline
# ==============================

def loop_baseline(summary: dict) -> tuple[str, list[str]]:
    """
    Derive an O(n^d) headline from loop nesting depth as a baseline.
    """
    loops = summary.get("loops", [])
    depth = max((int(l.get("depth", 1) or 1) for l in loops), default=0)
    headline = f"O(n^{depth})" if depth > 0 else "O(1)"
    expl = [f"Detected {len(loops)} loops; max depth = {depth}"]
    return headline, expl

# ==============================
# HTTP API
# ==============================

@app.route("/analyze", methods=["POST"])
def analyze():
    doc = request.get_json(silent=True)
    if not isinstance(doc, dict) or "summary" not in doc:
        return jsonify({"error": "invalid input"}), 400

    summary = doc["summary"]
    functions = summary.get("functions", [])
    recursive_names = [f["name"] for f in functions if isinstance(f, dict) and f.get("is_recursive")]
    headline, expl = loop_baseline(summary)
    expl.append(("recursive functions present: " + ", ".join(recursive_names)) if recursive_names
                else "no recursive functions detected")

    # 1) Try to extract a recurrence.
    rec = extract_recurrence(doc)
    recurrence_output = None
    adjusted_note = None
    rec_func_name = None

    if rec:
        a, b, f_expr, src, rec_func_name = rec

        # 1a) Try to upgrade f(n) using non-recursive callees of the recursive function (if we know it).
        # If we don't know which function, but there is exactly one recursive function, use that.
        if not rec_func_name:
            rec_func_name = pick_recursive_function_name(summary)

        inferred_f = infer_per_level_work(summary, rec_func_name) if rec_func_name else None
        if inferred_f:
            new_f, upgraded = upgrade_f_if_weaker(f_expr, inferred_f)
            if upgraded:
                adjusted_note = (f"Adjusted f(n) from parser hint ({f_expr}) to inferred {new_f} "
                                 f"based on non-recursive callee loops (function: {rec_func_name}).")
                f_expr = new_f

        # 1b) Solve via Master Theorem.
        recurrence_output = solve_master_theorem(a, b, f_expr)
        if "solution" in recurrence_output:
            headline = recurrence_output["solution"]
            lead = f"Solved via Master Theorem (from {src}) a={a}, b={b}, f(n)={f_expr}"
            if adjusted_note:
                expl.insert(0, adjusted_note)
            expl.insert(0, lead)

    # 2) If no solvable recurrence, tiny heuristic: recursion + no loops => linear.
    if not recurrence_output:
        has_recursive = bool(recursive_names)
        total_loops = sum(int(f.get("loopCount", 0) or 0) for f in functions)
        if has_recursive and total_loops == 0:
            headline = "O(n)"
            expl.insert(0, "Inferred recurrence: T(n)=T(n-1)+Θ(1) — no loops + recursion (linear fallback).")
            recurrence_output = {
                "recurrence": "T(n)=T(n-1)+Θ(1)",
                "solution": "O(n)",
                "case_reasoning": ["Linear recursion with constant work per step."]
            }

    result = {"complexity": headline, "explanation": expl}
    if recurrence_output:
        result["recurrence_solution"] = recurrence_output
    return jsonify(result)
