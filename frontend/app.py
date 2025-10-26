from flask import Flask, request, render_template
import requests, json

app = Flask(__name__)

PARSER_URL = "http://parser-c:7001/parse"
ANALYZER_URL = "http://analyzer:7100/analyze"

@app.route("/", methods=["GET", "POST"])
def index():
    code = ""
    ast_output = None       # dict (for pretty print)
    analysis_output = None  # dict (for pretty print)

    if request.method == "POST":
        code = request.form["code"]
        try:
            # 1) Parser: get AST + summary (JSON)
            parser_resp = requests.post(PARSER_URL, json={"language":"c","code":code})
            parser_json = parser_resp.json()  # <- dict
            ast_output = parser_json.get("ast", {})
            summary = parser_json.get("summary", {})

            # 2) Analyzer: send summary (JSON) and get result
            analyzer_resp = requests.post(ANALYZER_URL, json={"summary": summary})
            analysis_output = analyzer_resp.json()  # <- dict

        except Exception as e:
            analysis_output = {"error": f"contacting services failed: {e}"}

    return render_template("index.html", code=code, ast=ast_output, analysis=analysis_output)
